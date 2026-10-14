/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "SipBackend.h"

#include <Autolock.h>
#include <Message.h>
#include <OS.h>
#include <String.h>

#include <re.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <condition_variable>
#include <mutex>


// libre has no re_strdup; allocate a copy the re way.
static char*
_DupString(const char* string)
{
	if (string == NULL)
		return NULL;
	size_t length = strlen(string) + 1;
	char* copy = (char*)mem_alloc(length, NULL);
	if (copy != NULL)
		memcpy(copy, string, length);
	return copy;
}


// baresip reserves 16 bytes ahead of the payload: 12 for the RTP
// header plus 4 for the SRTP auth tag, so rtp_send can prepend in
// place without clobbering the payload.
enum { kRtpPresz = 16 };


// #pragma mark - G.711 codec (PCMU/PCMA)


// Sun g711-style conversions; table-free bit twiddling, 8 kHz mono.
static int16_t
_UlawToLinear(uint8_t ulaw)
{
	int16_t word;

	ulaw = ~ulaw;
	word = (ulaw & 0x0f) << 4;
	word += 8;
	word <<= ((ulaw & 0x70) >> 4);
	return (ulaw & 0x80) ? (8 - word) : (word - 8);
}


static uint8_t
_LinearToUlaw(int16_t sample)
{
	int16_t mask;
	int16_t clip;
	uint8_t uval;

	clip = sample < -32635 ? -32635 : (sample > 32635 ? 32635 : sample);
	if (clip < 0)
		clip = -clip - 8;
	mask = 0x0f;
	if (clip > 0x1fff)
		clip >>= 2, mask |= 0x30;
	if (clip > 0x3ff)
		clip >>= 2, mask |= 0x20;
	if (clip > 0xfff)
		clip >>= 2, mask |= 0x10;
	uval = (uint8_t)((clip >> 2) & 0x0f);
	return (uint8_t)~(uval | mask);
}


static int16_t
_AlawToLinear(uint8_t alaw)
{
	int16_t word;
	int16_t mask;

	alaw ^= 0x55;
	mask = alaw & 0x0f;
	word = (alaw & 0xf0) >> 4;
	if ((alaw & 0x80) == 0)
		word |= 0x100;
	word <<= 4;
	if (mask > 8) {
		mask = (mask << 1) & 0x10;
		word |= mask;
	}
	return (alaw & 0x80) != 0 ? word : -word;
}


static uint8_t
_LinearToAlaw(int16_t sample)
{
	int16_t mask;
	int16_t clip;

	clip = sample < -32635 ? -32635 : (sample > 32635 ? 32635 : sample);
	if (clip >= 0)
		mask = 0xD5;
	else {
		mask = 0x55;
		clip = -clip - 8;
	}
	clip >>= 4;
	mask |= (clip > 0x0f) ? (clip >> 4) & 0x07 : (clip & 0x0f);
	return (uint8_t)(mask ^ 0x55);
}


// #pragma mark - Shared libre loop


// libre has one process-wide main loop; every SipBackend instance in
// the process (the roster's singleton and the host test's two user
// agents) shares it. First user starts the thread; the mqueue marshals
// jobs from foreign threads onto it.
class _SipLoop {
public:
	static _SipLoop* Get()
	{
		static _SipLoop* instance = new _SipLoop();
		return instance;
	}

	void Acquire()
	{
		fMutex.lock();
		fRefCount++;
		if (fThread < 0) {
			fReady = false;
			fThread = spawn_thread(_Entry, "sip_loop",
				B_NORMAL_PRIORITY, this);
			resume_thread(fThread);
			fMutex.unlock();
			std::unique_lock<std::mutex> readyLock(fReadyMutex);
			while (!fReady)
				fReadyCondition.wait(readyLock);
			return;
		}
		fMutex.unlock();
	}

	void Release()
	{
		fMutex.lock();
		fRefCount--;
		if (fRefCount == 0 && fThread >= 0 && fQueue != NULL) {
			thread_id thread = fThread;
			fThread = -1;
			mqueue_push(fQueue, 0, NULL);
			fMutex.unlock();
			wait_for_thread(thread, NULL);
			return;
		}
		fMutex.unlock();
	}

	status_t Push(void (*func)(void*), void* arg)
	{
		if (fQueue == NULL)
			return B_NO_INIT;
		void** job = new void*[2];
		job[0] = (void*)func;
		job[1] = arg;
		if (mqueue_push(fQueue, 1, job) != 0) {
			delete[] job;
			return B_ERROR;
		}
		return B_OK;
	}

private:
	_SipLoop()
		:
		fRefCount(0),
		fThread(-1),
		fQueue(NULL),
		fReady(false)
	{
	}

	static int32 _Entry(void* data)
	{
		((_SipLoop*)data)->_Run();
		return 0;
	}

	static void _OnJob(int id, void* data, void* arg)
	{
		(void)arg;
		if (id == 0) {
			re_cancel();
			return;
		}
		void** job = (void**)data;
		void (*func)(void*) = (void (*)(void*))job[0];
		func(job[1]);
		delete[] job;
	}

	void _Run()
	{
		// libre requires an explicit init before any stack work.
		libre_init();
		mqueue_alloc(&fQueue, _OnJob, this);
		{
			std::lock_guard<std::mutex> guard(fReadyMutex);
			fReady = true;
		}
		fReadyCondition.notify_all();
		re_main(NULL);
		fQueue = NULL;
	}

public:
	std::mutex fMutex;
	int fRefCount;
	thread_id fThread;
	struct mqueue* fQueue;

private:
	std::mutex fReadyMutex;
	std::condition_variable fReadyCondition;
	bool fReady;
};


// #pragma mark - Account / Call


struct SipBackend::Account {
	SipBackend* backend;
	BString uri;
	BString authUser;
	BString password;
	BString displayName;
	BString proxy;
	BString stunServer;
	bool ice;
	bool tls;
	bool srtp;
	bool video;
	BString path;
	uint32 regState;
	bool registered;

	// Loop thread only.
	struct sipreg* reg;
	struct tmr regRetryTimer;
};


struct SipBackend::Call {
	SipBackend* backend;
	Account* account;
	BString path;
	BString peerUri;
	uint32 state;
	uint32 direction;
	bool localHold;
	bool destroying;

	// Loop thread only.
	struct sipsess* sess;
	struct sdp_session* sdp;
	struct sdp_media* audioMedia;
	struct sdp_media* videoMedia;
	struct rtp_sock* rtp;
	struct rtp_sock* vrtp;
	struct srtp* srtpSession;
	struct udp_helper* srtpHelper;
	struct telev* telev;
	struct tmr audioTimer;
	uint32 rtpTimestamp;
	uint32 ssrc;
	uint16 audioPt;
	uint16 televPt;
	uint16 videoPt;
	struct sa rtpAddr;
	bool rtpAddrValid;
	bool audioNegotiated;
	bool videoNegotiated;
	char lastDtmf[32];
	int dtmfLength;

	// SRTP SDES key material (30 raw bytes).
	uint8 srtpKey[30];
	bool srtpLocalKey;
	bool srtpActive;

	// ICE (audio component); NULL when disabled.
	struct icem* ice;
	bool iceControlling;

	// Pending STUN discovery for this call's media path.
	struct stun* stunMedia;
	struct udp_sock* stunSock;
	struct tmr stunTimeout;
	bool stunDone;
	bool pendingOffer;
	bool gathering;
};


// Job payloads.


struct AddAccountJob {
	SipBackend* backend;
	SipBackend::Account* account;
	std::mutex mutex;
	std::condition_variable done;
	bool complete;
	status_t result;
};


struct DialJob {
	SipBackend* backend;
	SipBackend::Account* account;
	BString uri;
	std::mutex mutex;
	std::condition_variable done;
	bool complete;
	status_t result;
	BString callPath;
};


struct CallOpJob {
	SipBackend* backend;
	SipBackend::Call* call;
	BMessenger replyTo;
	uint32 replyWhat;
	BString digits;
	BString text;
	BString number;
	int op;
	status_t result;
	std::mutex mutex;
	std::condition_variable done;
	bool complete;
};


enum {
	kOpAnswer = 1,
	kOpHangup,
	kOpHold,
	kOpDtmf,
	kOpSendMessage
};


// #pragma mark - Forward declarations


static void _AudioPump(void* arg);
static int _OnAuthChallenge(char** username, char** password,
	const char* realm, void* arg);
static void _OnRegResponse(int err, const struct sip_msg* msg, void* arg);
static void _OnSessionEstablished(const struct sip_msg* msg, void* arg);
static void _OnSessionProgress(const struct sip_msg* msg, void* arg);
static void _OnSessionClosed(int err, const struct sip_msg* msg, void* arg);
static int _OnSessionOffer(struct mbuf** descp, const struct sip_msg* msg,
	void* arg);
static int _OnSessionAnswer(const struct sip_msg* msg, void* arg);
static void _OnIncomingInvite(const struct sip_msg* msg, void* arg);
static void _OnRtpReceive(const struct sa* src,
	const struct rtp_header* hdr, struct mbuf* mb, void* arg);
static void _OnVideoReceive(const struct sa* src,
	const struct rtp_header* hdr, struct mbuf* mb, void* arg);


// #pragma mark - Address helpers


// The c= address we advertise: the first non-loopback IPv4, else
// loopback for host tests.
static bool
_GuessIfaddr(const char* ifname, const struct sa* sa, void* arg)
{
	(void)ifname;
	struct sa* out = (struct sa*)arg;
	if (sa_af(sa) != AF_INET)
		return false;
	if (sa_is_loopback(sa))
		return false;
	sa_cpy(out, sa);
	sa_set_port(out, 0);
	return true;
}


static void
_MediaLaddr(SipBackend* backend, struct sa* out)
{
	if (backend != NULL && !backend->fSipBindAddress.IsEmpty()) {
		sa_set_str(out, backend->fSipBindAddress.String(), 0);
		return;
	}
	sa_set_str(out, "127.0.0.1", 0);
	net_if_list(_GuessIfaddr, out);
}


// #pragma mark - SRTP udp helpers


static bool
_SrtpSendHelper(int* err, struct sa* dst, struct mbuf* mb, void* arg)
{
	SipBackend::Call* call = (SipBackend::Call*)arg;
	(void)err;
	(void)dst;

	if (call->srtpSession == NULL || mbuf_get_left(mb) < 2)
		return false;
	// Only RTP/RTCP carry the version bits; STUN keeps its cookie.
	uint8 first = *mbuf_buf(mb);
	if ((first >> 6) != RTP_VERSION)
		return false;
	if (srtp_encrypt(call->srtpSession, mb) != 0)
		return false;
	return false;
}


static bool
_SrtpRecvHelper(struct sa* src, struct mbuf* mb, void* arg)
{
	SipBackend::Call* call = (SipBackend::Call*)arg;
	(void)src;

	if (call->srtpSession == NULL || mbuf_get_left(mb) < 2)
		return false;
	uint8 first = *mbuf_buf(mb);
	if ((first >> 6) != RTP_VERSION)
		return false;
	srtp_decrypt(call->srtpSession, mb);
	return false;
}


// #pragma mark - SDP


static int
_BuildAudioSession(SipBackend::Call* call, bool includeVideo)
{
	struct sa laddr;
	_MediaLaddr(call->account->backend, &laddr);

	if (call->sdp != NULL)
		return 0;

	struct sdp_session* sdp = NULL;
	sdp_session_alloc(&sdp, &laddr);
	call->sdp = sdp;

	struct sdp_media* media = NULL;
	sdp_media_add(&media, sdp, sdp_media_audio, 0, sdp_proto_rtpavp);
	call->audioMedia = media;

	// PCMU and PCMA are mandatory in SIP; telephone-event carries
	// RFC 4733 DTMF.
	sdp_format_add(NULL, media, true, "0", "PCMU", 8000, 1, NULL, NULL,
		NULL, false, NULL);
	sdp_format_add(NULL, media, false, "8", "PCMA", 8000, 1, NULL, NULL,
		NULL, false, NULL);
	sdp_format_add(NULL, media, false, "101", "telephone-event", 8000, 1,
		NULL, NULL, NULL, false, "0-16");

	if (includeVideo && call->account->video) {
		struct sdp_media* video = NULL;
		sdp_media_add(&video, sdp, sdp_media_video, 0,
			sdp_proto_rtpavp);
		call->videoMedia = video;
		sdp_format_add(NULL, video, true, "96", "VP8", 90000, 1, NULL,
			NULL, NULL, false, NULL);
	}

	// SRTP (SDES): advertise a crypto attribute when asked.
	if (call->account->srtp) {
		char keyB64[64];
		size_t b64Len = sizeof(keyB64);
		rand_bytes(call->srtpKey, 30);
		call->srtpLocalKey = true;
		base64_encode(call->srtpKey, 30, keyB64, &b64Len);
		sdp_media_set_lattr(media, true, "crypto",
			"1 AES_CM_128_HMAC_SHA1_80 inline:%s", keyB64);
	}

	return 0;
}


static int
_EncodeSdp(SipBackend::Call* call, struct mbuf** descp, bool offer)
{
	if (call->audioMedia != NULL && call->rtp != NULL) {
		const struct sa* local = rtp_local(call->rtp);
		sdp_media_set_lport(call->audioMedia, sa_port(local));
	}
	if (call->videoMedia != NULL && call->vrtp != NULL) {
		const struct sa* local = rtp_local(call->vrtp);
		sdp_media_set_lport(call->videoMedia, sa_port(local));
	}
	return sdp_encode(descp, call->sdp, offer);
}


static void
_DecodeRemoteSdp(SipBackend::Call* call, struct mbuf* body, bool offer)
{
	sdp_decode(call->sdp, body, offer);

	const struct sa* raddr = sdp_media_raddr(call->audioMedia);
	uint16 rport = sdp_media_rport(call->audioMedia);
	if (raddr != NULL && rport > 0) {
		sa_cpy(&call->rtpAddr, raddr);
		sa_set_port(&call->rtpAddr, rport);
		call->rtpAddrValid = true;
	}

	// srate/ch of -1 mean "any"; 0 would filter them out.
	const struct sdp_format* fmt = sdp_media_format(call->audioMedia,
		false, NULL, 0, NULL, -1, -1);
	if (fmt == NULL)
		fmt = sdp_media_format(call->audioMedia, false, NULL, 8,
			NULL, -1, -1);
	if (fmt != NULL) {
		call->audioPt = (uint16)fmt->pt;
		call->audioNegotiated = true;
	}
	fmt = sdp_media_format(call->audioMedia, false, NULL, 101, NULL,
		-1, -1);
	if (fmt != NULL)
		call->televPt = (uint16)fmt->pt;

	// Pick up the remote crypto key if SRTP was negotiated.
	if (call->account->srtp && !call->srtpActive) {
		const char* crypto = sdp_media_rattr(call->audioMedia,
			"crypto");
		if (crypto != NULL) {
			const char* inlineKey = strstr(crypto, "inline:");
			if (inlineKey != NULL) {
				char keyB64[64];
				uint8 key[30];
				size_t keyLen = sizeof(key);
				snprintf(keyB64, sizeof(keyB64), "%s",
					inlineKey + 7);
				char* space = strchr(keyB64, ' ');
				if (space != NULL)
					*space = '\0';
				if (base64_decode(keyB64, strlen(keyB64), key,
						&keyLen) == 0 && keyLen == 30) {
					// SDES: each side uses the key from
					// the offerer for send and receive
					// with the reversed direction salt;
					// re's srtp takes the raw key.
					memcpy(call->srtpKey, key, 30);
					call->srtpActive = true;
				}
			}
		}
	}

	if (call->videoMedia != NULL)
		call->videoNegotiated =
			sdp_media_format(call->videoMedia, false, NULL, 96,
				NULL, -1, -1) != NULL;
}


// #pragma mark - Media plumbing


static void
_SetupRtp(SipBackend::Call* call)
{
	if (call->rtp != NULL)
		return;

	struct sa any;
	sa_set_str(&any, "0.0.0.0", 0);
	rtp_listen(&call->rtp, IPPROTO_UDP, &any, 10000, 60000, false,
		&_OnRtpReceive, NULL, call);
	telev_alloc(&call->telev, 20);

	// Video rides the next port up when the account asked for it.
	if (call->account->video) {
		rtp_listen(&call->vrtp, IPPROTO_UDP, &any, 10000, 60000, false,
			&_OnVideoReceive, NULL, call);
	}

	if (call->account->srtp && call->srtpActive) {
		srtp_alloc(&call->srtpSession, SRTP_AES_CM_128_HMAC_SHA1_80,
			call->srtpKey, 30, 0);
		udp_register_helper(&call->srtpHelper,
			(struct udp_sock*)rtp_sock(call->rtp), 5,
			&_SrtpSendHelper, &_SrtpRecvHelper, call);
	}
}


static void
_OnRtpReceive(const struct sa* src, const struct rtp_header* hdr,
	struct mbuf* mb, void* arg)
{
	SipBackend::Call* call = (SipBackend::Call*)arg;

	// Symmetric RTP: learn the far end from the first packet when
	// the SDP did not pin it down (ICE overrides this later).
	if (!call->rtpAddrValid) {
		sa_cpy(&call->rtpAddr, src);
		call->rtpAddrValid = true;
	}

	if (hdr->pt == call->televPt) {
		int event = 0;
		bool end = false;
		int televErr = telev_recv(call->telev, mb, &event, &end);
		if (televErr == 0 && end) {
			int digit = telev_code2digit(event);
			if (digit >= 0 && call->dtmfLength
					< (int)sizeof(call->lastDtmf) - 1) {
				call->lastDtmf[call->dtmfLength++] = (char)digit;
				call->lastDtmf[call->dtmfLength] = '\0';
			}
		}
		return;
	}

	if (!call->audioNegotiated || call->backend->fAudioPush == NULL)
		return;

	int16 pcm[160];
	size_t count = 0;
	while (mbuf_get_left(mb) > 0 && count < 160) {
		uint8 byte = mbuf_read_u8(mb);
		pcm[count++] = hdr->pt == 0
			? _UlawToLinear(byte) : _AlawToLinear(byte);
	}
	if (count > 0)
		call->backend->fAudioPush(call->backend->fAudioCookie, pcm,
			count);
}


// Video RTP rides a second socket; received VP8 payloads go to the
// decoder glue (SipVideo.cpp) through the payload handler hook.
static void
_OnVideoReceive(const struct sa* src, const struct rtp_header* hdr,
	struct mbuf* mb, void* arg)
{
	SipBackend::Call* call = (SipBackend::Call*)arg;
	(void)src;

	if (call->backend->fVideoPayloadHandler == NULL)
		return;

	size_t size = mbuf_get_left(mb);
	if (size == 0)
		return;
	const uint8* payload = mbuf_buf(mb);
	bool keyFrame = size > 0 && (payload[0] & 0x10) != 0;
	call->backend->fVideoPayloadHandler(
		call->backend->fVideoPayloadCookie, payload, size, keyFrame);
}


static void
_AudioPump(void* arg)
{
	SipBackend::Call* call = (SipBackend::Call*)arg;
	SipBackend* backend = call->backend;

	if (call->state == kMMCallStateActive && !call->destroying)
		tmr_start(&call->audioTimer, 20, _AudioPump, call);

	if (call->state != kMMCallStateActive || call->destroying
			|| !call->audioNegotiated || !call->rtpAddrValid)
		return;

	// One RFC 4733 packet per tick paces the event correctly. The
	// payload lands after RTP_HEADER_SIZE so rtp_send can prepend
	// the header in place.
	if (call->telev != NULL) {
		bool marker = false;
		struct mbuf* packet = mbuf_alloc(kRtpPresz + 32);
		if (packet != NULL) {
			packet->pos = kRtpPresz;
			packet->end = kRtpPresz;
		}
		int pollErr = telev_poll(call->telev, &marker, packet);
		if (pollErr == 0 && packet != NULL) {
			// rtp_send rewinds from pos, so point it back at
			// the payload start.
			packet->pos = kRtpPresz;
			rtp_send(call->rtp, &call->rtpAddr, false, marker,
				call->televPt, call->rtpTimestamp, packet);
		}
		mem_deref(packet);
	}

	if (call->localHold || backend->fAudioPull == NULL)
		return;

	int16 pcm[160];
	size_t got = backend->fAudioPull(backend->fAudioCookie, pcm, 160);
	if (got == 0)
		return;

	struct mbuf* packet = mbuf_alloc(kRtpPresz + 160);
	if (packet == NULL)
		return;
	packet->pos = kRtpPresz;
	packet->end = kRtpPresz;
	for (size_t i = 0; i < got; i++) {
		uint8 byte = call->audioPt == 0
			? _LinearToUlaw(pcm[i]) : _LinearToAlaw(pcm[i]);
		mbuf_write_u8(packet, byte);
	}
	packet->pos = kRtpPresz;
	rtp_send(call->rtp, &call->rtpAddr, false, false, call->audioPt,
		call->rtpTimestamp, packet);
	mem_deref(packet);
	call->rtpTimestamp += 160;
}


// #pragma mark - Session callbacks


static int
_OnSessionOffer(struct mbuf** descp, const struct sip_msg* msg, void* arg)
{
	SipBackend::Call* call = (SipBackend::Call*)arg;

	if (call->sdp == NULL)
		return EINVAL;

	// A re-INVITE (hold/resume) carries a new remote description;
	// decode it before encoding our answer so direction bits and
	// any changed media address take effect.
	if (msg != NULL && mbuf_get_left(msg->mb) > 0) {
		struct mbuf* clone = mbuf_alloc(mbuf_get_left(msg->mb));
		if (clone != NULL) {
			mbuf_write_mem(clone, mbuf_buf(msg->mb),
				mbuf_get_left(msg->mb));
			clone->pos = 0;
			_DecodeRemoteSdp(call, clone, true);
			mem_deref(clone);
		}
	}

	sdp_media_set_ldir(call->audioMedia, call->localHold
		? SDP_SENDONLY : SDP_SENDRECV);
	return _EncodeSdp(call, descp, false);
}


static int
_OnSessionAnswer(const struct sip_msg* msg, void* arg)
{
	SipBackend::Call* call = (SipBackend::Call*)arg;

	if (msg != NULL && call->sdp != NULL
			&& mbuf_get_left(msg->mb) > 0) {
		struct mbuf* clone = mbuf_alloc(mbuf_get_left(msg->mb));
		if (clone != NULL) {
			mbuf_write_mem(clone, mbuf_buf(msg->mb),
				mbuf_get_left(msg->mb));
			clone->pos = 0;
			_DecodeRemoteSdp(call, clone, false);
			mem_deref(clone);
		}
	}
	_SetupRtp(call);
	return 0;
}


static void
_OnSessionEstablished(const struct sip_msg* msg, void* arg)
{
	SipBackend::Call* call = (SipBackend::Call*)arg;

	if (msg != NULL && call->sdp != NULL
			&& mbuf_get_left(msg->mb) > 0
			&& call->state == kMMCallStateDialing) {
		struct mbuf* clone = mbuf_alloc(mbuf_get_left(msg->mb));
		if (clone != NULL) {
			mbuf_write_mem(clone, mbuf_buf(msg->mb),
				mbuf_get_left(msg->mb));
			clone->pos = 0;
			_DecodeRemoteSdp(call, clone, false);
			mem_deref(clone);
		}
	}
	_SetupRtp(call);

	call->state = kMMCallStateActive;

	BMessage notification(SipBackend::NOTIFICATION_CALL_STATE_CHANGED);
	notification.AddString(kMMFieldCallPath, call->path.String());
	notification.AddString(kMMFieldCallModemPath,
		call->account->path.String());
	notification.AddString(kMMFieldCallNumber, call->peerUri.String());
	notification.AddUInt32(kMMFieldCallState, kMMCallStateActive);
	notification.AddString(kMMFieldCallStateName, "active");
	notification.AddUInt32(kMMFieldCallDirection, call->direction);
	call->backend->_NotifyWatchers(
		SipBackend::NOTIFICATION_CALL_STATE_CHANGED, notification);

	tmr_start(&call->audioTimer, 20, _AudioPump, call);

	// Kick off the camera when this call negotiated video and the
	// app pointed us at a capture device.
	if (call->videoNegotiated && !call->backend->fVideoDevice.IsEmpty())
		SipBackend_StartVideoCapture(call->backend,
			call->backend->fVideoDevice.String());
}


static void
_OnSessionProgress(const struct sip_msg* msg, void* arg)
{
	SipBackend::Call* call = (SipBackend::Call*)arg;
	if (msg == NULL || call->state != kMMCallStateDialing)
		return;
	if (msg->scode < 180 || msg->scode >= 300)
		return;

	call->state = kMMCallStateRingingOut;

	BMessage notification(SipBackend::NOTIFICATION_CALL_STATE_CHANGED);
	notification.AddString(kMMFieldCallPath, call->path.String());
	notification.AddString(kMMFieldCallModemPath,
		call->account->path.String());
	notification.AddString(kMMFieldCallNumber, call->peerUri.String());
	notification.AddUInt32(kMMFieldCallState, kMMCallStateRingingOut);
	notification.AddString(kMMFieldCallStateName, "ringing out");
	notification.AddUInt32(kMMFieldCallDirection, call->direction);
	call->backend->_NotifyWatchers(
		SipBackend::NOTIFICATION_CALL_STATE_CHANGED, notification);
}


static void
_OnSessionClosed(int err, const struct sip_msg* msg, void* arg)
{
	SipBackend::Call* call = (SipBackend::Call*)arg;
	call->state = kMMCallStateTerminated;

	BMessage notification(SipBackend::NOTIFICATION_CALL_STATE_CHANGED);
	notification.AddString(kMMFieldCallPath, call->path.String());
	notification.AddString(kMMFieldCallModemPath,
		call->account->path.String());
	notification.AddString(kMMFieldCallNumber, call->peerUri.String());
	notification.AddUInt32(kMMFieldCallState, kMMCallStateTerminated);
	notification.AddString(kMMFieldCallStateName, "terminated");
	notification.AddUInt32(kMMFieldCallDirection, call->direction);
	call->backend->_NotifyWatchers(
		SipBackend::NOTIFICATION_CALL_STATE_CHANGED, notification);

	call->backend->_DestroyCall(call);
}


static void
_OnIncomingInvite(const struct sip_msg* msg, void* arg)
{
	SipBackend* backend = (SipBackend*)arg;
	backend->_IncomingCall(msg);
}


// #pragma mark - SIP MESSAGE


// Handles inbound MESSAGE requests: a text body becomes an SMS-shaped
// entry so the Messages app shows SIP and cellular messages in the
// same thread list.
static bool
_OnSipMessage(const struct sip_msg* msg, void* arg)
{
	SipBackend* backend = (SipBackend*)arg;
	if (msg == NULL || pl_strcmp(&msg->met, "MESSAGE") != 0)
		return false;

	sip_reply((struct sip*)backend->fSipStack, msg, 200, "OK");

	SipBackend::Account* account = NULL;
	{
		BAutolock lock(backend->fLock);
		for (std::map<BString, SipBackend::Account*>::iterator it
				= backend->fAccounts.begin();
				it != backend->fAccounts.end(); it++) {
			account = it->second;
			break;
		}
	}
	if (account == NULL)
		return true;

	char from[256];
	snprintf(from, sizeof(from), "%.*s@%.*s",
		(int)msg->from.uri.user.l, msg->from.uri.user.p,
		(int)msg->from.uri.host.l, msg->from.uri.host.p);

	char text[1024];
	text[0] = '\0';
	if (mbuf_get_left(msg->mb) > 0) {
		size_t length = mbuf_get_left(msg->mb);
		if (length >= sizeof(text))
			length = sizeof(text) - 1;
		mbuf_read_mem(msg->mb, (uint8*)text, length);
		text[length] = '\0';
	}

	BMessage stored;
	stored.AddString(kMMFieldSmsPath, "sip-msg:in");
	stored.AddString(kMMFieldSmsModemPath, account->path.String());
	stored.AddString(kMMFieldSmsNumber, from);
	stored.AddString(kMMFieldSmsText, text);
	stored.AddUInt32(kMMFieldSmsState, kMMSmsStateReceived);
	stored.AddString(kMMFieldSmsStateName, "received");
	stored.AddBool(kMMFieldSmsReceived, true);
	stored.AddUInt32(kMMFieldSmsPduType, kMMSmsPduTypeDeliver);
	{
		BAutolock lock(backend->fLock);
		backend->fSmsLog.push_back(stored);
	}

	BMessage notification(SipBackend::NOTIFICATION_SMS_ADDED);
	notification.AddString(kMMFieldSmsPath, "sip-msg:in");
	notification.AddString(kMMFieldSmsModemPath, account->path.String());
	notification.AddString(kMMFieldSmsNumber, from);
	notification.AddString(kMMFieldSmsText, text);
	notification.AddUInt32(kMMFieldSmsState, kMMSmsStateReceived);
	notification.AddBool(kMMFieldSmsReceived, true);
	backend->_NotifyWatchers(SipBackend::NOTIFICATION_SMS_ADDED,
		notification);
	return true;
}


// #pragma mark - STUN / ICE


// STUN binding runs asynchronously on the shared loop; plain STUN replaces the
// c= address, ICE adds a server-reflexive candidate.
static void
_OnStunResponse(int err, uint16_t scode, const char* reason,
	const struct stun_msg* msg, void* arg)
{
	SipBackend::Call* call = (SipBackend::Call*)arg;
	(void)reason;

	struct sa mapped;
	sa_set_str(&mapped, "0.0.0.0", 0);

	if (err == 0 && msg != NULL && scode == 0) {
		struct stun_attr* attr = stun_msg_attr(msg,
			STUN_ATTR_XOR_MAPPED_ADDR);
		if (attr == NULL)
			attr = stun_msg_attr(msg, STUN_ATTR_MAPPED_ADDR);
		if (attr != NULL)
			sa_cpy(&mapped, &attr->v.mapped_addr);
	}

	call->stunDone = true;
	tmr_cancel(&call->stunTimeout);
	mem_deref(call->stunSock);
	call->stunSock = NULL;
	mem_deref(call->stunMedia);
	call->stunMedia = NULL;

	call->backend->_OnStunGathered(call, call->pendingOffer,
		sa_af(&mapped) != AF_UNSPEC ? &mapped : NULL);
}


static void
_OnStunTimeout(void* arg)
{
	SipBackend::Call* call = (SipBackend::Call*)arg;
	call->stunDone = true;
	mem_deref(call->stunSock);
	call->stunSock = NULL;
	mem_deref(call->stunMedia);
	call->stunMedia = NULL;
	call->backend->_OnStunGathered(call, call->pendingOffer, NULL);
}


void
SipBackend::_StartStunGather(Call* call, bool offer)
{
	Account* account = call->account;
	call->pendingOffer = offer;

	if (account->stunServer.IsEmpty()) {
		_OnStunGathered(call, offer, NULL);
		return;
	}

	struct sa stunAddr;
	if (sa_set_str(&stunAddr, account->stunServer.String(), 3478) != 0) {
		_OnStunGathered(call, offer, NULL);
		return;
	}

	struct stun_conf conf;
	memset(&conf, 0, sizeof(conf));
	conf.rto = 200;
	conf.rc = 3;
	conf.rm = 4;

	call->stunDone = false;
	if (stun_alloc(&call->stunMedia, &conf, NULL, NULL) != 0) {
		_OnStunGathered(call, offer, NULL);
		return;
	}
	if (udp_open(&call->stunSock, AF_INET) != 0) {
		mem_deref(call->stunMedia);
		call->stunMedia = NULL;
		_OnStunGathered(call, offer, NULL);
		return;
	}

	tmr_start(&call->stunTimeout, 2000, _OnStunTimeout, call);
	stun_request(NULL, call->stunMedia, IPPROTO_UDP, call->stunSock,
		&stunAddr, 0, STUN_METHOD_BINDING, NULL, 0, false,
		&_OnStunResponse, call, 0);
}


void
SipBackend::_OnStunGathered(Call* call, bool offer, const void* mappedSa)
{
	if (mappedSa != NULL) {
		// Overwrite the advertised c= address with the reflexive
		// one so media reaches us through the NAT.
		sa_cpy(&call->rtpAddr, (const struct sa*)mappedSa);
		call->rtpAddrValid = true;
	}

	if (call->ice != NULL && mappedSa != NULL) {
		icem_cand_add(call->ice, ICE_COMPID_RTP, 7000, NULL,
			(const struct sa*)mappedSa);
		icem_conncheck_start(call->ice);
	}

	if (offer)
		_SendOffer(call);
	else
		_SendAnswer(call);
}


// #pragma mark - Registration


static void
_OnRegResponse(int err, const struct sip_msg* msg, void* arg)
{
	SipBackend::Account* account = (SipBackend::Account*)arg;
	uint32 state = account->regState;
	bool registered = account->registered;

	if (err == EPROTO && msg != NULL && msg->scode == 401) {
		// Digest challenge: sipreg retries with credentials.
		state = kMMRegSearching;
	} else if (err == 0 && msg != NULL && msg->scode >= 200
			&& msg->scode < 300) {
		state = kMMRegHome;
		registered = true;
	} else if (err == 0 && msg != NULL && msg->scode == 403) {
		state = kMMRegDenied;
		registered = false;
	} else if (err == 0 && msg != NULL && msg->scode >= 300) {
		state = kMMRegUnknown;
		registered = false;
	} else if (err != 0 && err != EAGAIN && err != EINTR) {
		state = kMMRegUnknown;
		registered = false;
	} else
		return;

	account->regState = state;
	account->registered = registered;

	BMessage notification(SipBackend::NOTIFICATION_REGISTRATION_CHANGED);
	notification.AddString(kMMFieldModemPath, account->path.String());
	notification.AddUInt32(kMMFieldModemRegistrationState, state);
	notification.AddBool(kMMFieldModemEnabled, true);
	account->backend->_NotifyWatchers(
		SipBackend::NOTIFICATION_REGISTRATION_CHANGED, notification);
}


static int
_OnAuthChallenge(char** username, char** password, const char* realm,
	void* arg)
{
	SipBackend::Account* account = (SipBackend::Account*)arg;
	(void)realm;

	*username = _DupString(account->authUser.String());
	*password = _DupString(account->password.String());
	return (*username != NULL && *password != NULL) ? 0 : ENOMEM;
}


// #pragma mark - Jobs


static void
_CreateAccountJob(void* arg)
{
	AddAccountJob* job = (AddAccountJob*)arg;
	SipBackend* backend = job->backend;
	SipBackend::Account* account = job->account;

	int err = 0;
	if (backend->fSipStack == NULL) {
		struct dnsc* dnsc = NULL;
		dnsc_alloc(&dnsc, NULL, NULL, AF_INET);
		// The hash tables behind these sizes want power-of-two
		// bucket counts; zero would be EINVAL.
		err = sip_alloc((struct sip**)&backend->fSipStack, dnsc, 32,
			32, 32, "VitruvianOS SipBackend", NULL, NULL);

		struct sa laddr;
		_MediaLaddr(backend, &laddr);
		sa_set_port(&laddr, backend->fSipPort);
		err = sip_transp_add((struct sip*)backend->fSipStack,
			SIP_TRANSP_UDP, &laddr);

		if (account->tls) {
			struct tls* tlsConfig = NULL;
			tls_alloc(&tlsConfig, TLS_METHOD_SSLV23, NULL, NULL);
			tls_set_selfsigned(tlsConfig, "v-os-sip");
			tls_add_ca(tlsConfig,
				"/etc/ssl/certs/ca-certificates.crt");
			backend->fTlsConfig = tlsConfig;
			sip_transp_add((struct sip*)backend->fSipStack,
				SIP_TRANSP_TLS, &laddr, tlsConfig);
		}

		sipsess_listen((struct sipsess_sock**)&backend->fSessionSock,
			(struct sip*)backend->fSipStack, 8, &_OnIncomingInvite,
			backend);

		// MESSAGE requests ride a plain SIP listener next to the
		// session socket.
		sip_listen((struct sip_lsnr**)&backend->fMessageLsnr,
			(struct sip*)backend->fSipStack, true, &_OnSipMessage,
			backend);
	}

	struct sip* sip = (struct sip*)backend->fSipStack;

	BString regUri;
	regUri << "sip:" << account->uri.String();
	const char* routev[1] = { NULL };
	uint32_t routec = 0;
	if (!account->proxy.IsEmpty()) {
		routev[0] = account->proxy.String();
		routec = 1;
	}

	char fromUri[256];
	snprintf(fromUri, sizeof(fromUri), "%s", account->uri.String());

	// sipreg wants a non-NULL instance id (cuser); the auth
	// user is a stable choice.
	err = sipreg_register((struct sipreg**)&account->reg, sip,
		regUri.String(), fromUri, account->displayName.String(),
		fromUri, 600, account->authUser.String(), routev, routec,
		-1, &_OnAuthChallenge, account, true, &_OnRegResponse,
		account, NULL, NULL);
	if (err != 0)
		fprintf(stderr, "SipBackend: sipreg_register failed: %d\n", err);
	job->result = err == 0 ? B_OK : B_ERROR;
	job->complete = true;
	job->done.notify_all();
}


static void
_DialJob(void* arg)
{
	DialJob* job = (DialJob*)arg;
	SipBackend* backend = job->backend;
	SipBackend::Account* account = job->account;

	if (backend->fSipStack == NULL || backend->fSessionSock == NULL) {
		job->result = B_NO_INIT;
		job->complete = true;
		job->done.notify_all();
		return;
	}

	SipBackend::Call* call = new SipBackend::Call();
	call->backend = backend;
	call->account = account;
	call->state = kMMCallStateDialing;
	call->direction = kMMCallDirectionOutgoing;
	call->localHold = false;
	call->destroying = false;
	call->sess = NULL;
	call->sdp = NULL;
	call->audioMedia = NULL;
	call->videoMedia = NULL;
	call->rtp = NULL;
	call->vrtp = NULL;
	call->srtpSession = NULL;
	call->srtpHelper = NULL;
	call->telev = NULL;
	call->ice = NULL;
	call->iceControlling = false;
	call->stunMedia = NULL;
	call->stunSock = NULL;
	call->stunDone = true;
	call->pendingOffer = false;
	call->gathering = false;
	call->rtpTimestamp = 0;
	call->ssrc = rand_u32();
	call->audioPt = 0;
	call->televPt = 101;
	call->videoPt = 96;
	call->rtpAddrValid = false;
	call->audioNegotiated = false;
	call->videoNegotiated = false;
	call->dtmfLength = 0;
	call->lastDtmf[0] = '\0';
	call->srtpLocalKey = false;
	call->srtpActive = false;
	tmr_init(&call->audioTimer);
	tmr_init(&call->stunTimeout);

	char path[64];
	snprintf(path, sizeof(path), "sip-call:%d", backend->fNextCallId++);
	call->path = path;
	call->peerUri = job->uri;

	{
		BAutolock lock(backend->fLock);
		backend->fCalls[call->path] = call;
	}

	BMessage added(SipBackend::NOTIFICATION_CALL_ADDED);
	added.AddString(kMMFieldCallPath, call->path.String());
	added.AddString(kMMFieldCallModemPath, account->path.String());
	added.AddString(kMMFieldCallNumber, call->peerUri.String());
	added.AddUInt32(kMMFieldCallState, kMMCallStateDialing);
	added.AddString(kMMFieldCallStateName, "dialing");
	added.AddUInt32(kMMFieldCallDirection, kMMCallDirectionOutgoing);
	backend->_NotifyWatchers(SipBackend::NOTIFICATION_CALL_ADDED, added);

	backend->_ConnectSession(call);

	job->callPath = call->path;
	job->result = B_OK;
	job->complete = true;
	job->done.notify_all();
}


static void
_CallOpJob(void* arg)
{
	CallOpJob* job = (CallOpJob*)arg;
	SipBackend::Call* call = job->call;

	switch (job->op) {
		case kOpAnswer:
			call->backend->_AnswerCall(call);
			job->result = B_OK;
			break;
		case kOpHangup:
			call->backend->_HangupCall(call);
			job->result = B_OK;
			break;
		case kOpHold:
			call->backend->_HoldCall(call);
			job->result = B_OK;
			break;
		case kOpDtmf:
			call->backend->_SendDtmf(call, job->digits.String());
			job->result = B_OK;
			break;
		case kOpSendMessage:
		{
			SipBackend* backend = call->backend;
			BString head;
			head << "Content-Type: text/plain\r\n"
				"Content-Length: " << (int)job->text.Length()
				<< "\r\n\r\n" << job->text;
			int err = sip_requestf(NULL,
				(struct sip*)backend->fSipStack, true, "MESSAGE",
				job->number.String(), NULL, NULL, NULL, NULL,
				NULL, "%s", head.String());
			job->result = err == 0 ? B_OK : B_ERROR;

			BMessage stored;
			stored.AddString(kMMFieldSmsPath, "sip-msg:out");
			stored.AddString(kMMFieldSmsModemPath,
				call->account->path.String());
			stored.AddString(kMMFieldSmsNumber, job->number.String());
			stored.AddString(kMMFieldSmsText, job->text.String());
			stored.AddUInt32(kMMFieldSmsState,
				err == 0 ? kMMSmsStateSent : kMMSmsStateUnknown);
			stored.AddBool(kMMFieldSmsReceived, false);
			{
				BAutolock lock(backend->fLock);
				backend->fSmsLog.push_back(stored);
			}

			BMessage notification(
				SipBackend::NOTIFICATION_SMS_STATE_CHANGED);
			notification.AddString(kMMFieldSmsModemPath,
				call->account->path.String());
			notification.AddString(kMMFieldSmsNumber,
				job->number.String());
			notification.AddUInt32(kMMFieldSmsState,
				err == 0 ? kMMSmsStateSent : kMMSmsStateUnknown);
			notification.AddString(kMMFieldSmsStateName,
				err == 0 ? "sent" : "failed");
			notification.AddBool(kMMFieldSmsReceived, false);
			backend->_NotifyWatchers(
				SipBackend::NOTIFICATION_SMS_STATE_CHANGED,
				notification);
			break;
		}
		default:
			job->result = B_BAD_VALUE;
			break;
	}

	BMessage reply(job->replyWhat);
	reply.AddInt32("status", job->result);
	job->replyTo.SendMessage(&reply);

	job->complete = true;
	job->done.notify_all();
}


static CallOpJob*
_QueueCallOp(SipBackend* backend, const char* callPath, int op,
	const BMessenger& replyTo, uint32 replyWhat)
{
	SipBackend::Call* call = NULL;
	{
		BAutolock lock(backend->fLock);
		std::map<BString, SipBackend::Call*>::iterator it
			= backend->fCalls.find(callPath);
		if (it == backend->fCalls.end())
			return NULL;
		call = it->second;
	}

	CallOpJob* job = new CallOpJob();
	job->backend = backend;
	job->call = call;
	job->replyTo = replyTo;
	job->replyWhat = replyWhat;
	job->op = op;
	job->result = B_OK;
	job->complete = false;

	if (backend->PostJob(_CallOpJob, job) != B_OK) {
		delete job;
		return NULL;
	}
	return job;
}


// #pragma mark - SipBackend


SipBackend*
SipBackend::Instance()
{
	// Deliberately leaked; see ModemManagerBackend::Instance().
	static SipBackend* instance = new SipBackend();
	return instance;
}


SipBackend::SipBackend()
	:
	fSipStack(NULL),
	fSessionSock(NULL),
	fMessageLsnr(NULL),
	fTlsConfig(NULL),
	fSipPort(0),
	fNextCallId(1),
	fAudioPull(NULL),
	fAudioPush(NULL),
	fAudioCookie(NULL),
	fVideoRemote(NULL),
	fVideoLocal(NULL),
	fVideoCookie(NULL),
	fVideoPayloadHandler(NULL),
	fVideoPayloadCookie(NULL)
{
	_SipLoop::Get()->Acquire();
}


SipBackend::~SipBackend()
{
	_SipLoop::Get()->Release();
}


status_t
SipBackend::PostJob(void (*func)(void*), void* arg)
{
	return _SipLoop::Get()->Push(func, arg);
}


status_t
SipBackend::AddAccount(const BMessage& settings, const char* password,
	BString* accountPath)
{
	Account* account = new Account();
	account->backend = this;
	account->ice = false;
	account->tls = false;
	account->srtp = false;
	account->video = false;
	account->regState = kMMRegIdle;
	account->registered = false;
	account->reg = NULL;
	tmr_init(&account->regRetryTimer);

	const char* uri = NULL;
	settings.FindString(kSipFieldUri, &uri);
	if (uri == NULL || uri[0] == '\0') {
		delete account;
		return B_BAD_VALUE;
	}
	account->uri = uri;

	const char* value = NULL;
	if (settings.FindString(kSipFieldAuthUser, &value) == B_OK)
		account->authUser = value;
	else
		account->authUser = uri;
	if (settings.FindString(kSipFieldDisplayName, &value) == B_OK)
		account->displayName = value;
	if (settings.FindString(kSipFieldOutboundProxy, &value) == B_OK)
		account->proxy = value;
	if (settings.FindString(kSipFieldStunServer, &value) == B_OK)
		account->stunServer = value;
	settings.FindBool(kSipFieldIceEnabled, &account->ice);
	settings.FindBool(kSipFieldTlsEnabled, &account->tls);
	settings.FindBool(kSipFieldSrtpEnabled, &account->srtp);
	settings.FindBool(kSipFieldVideoEnabled, &account->video);
	account->password = password != NULL ? password : "";

	// Line path: sip:user@domain, scheme stripped for readability.
	const char* aor = strstr(uri, "sip:");
	account->path = "sip:";
	account->path << (aor != NULL ? aor + 4 : uri);

	AddAccountJob job;
	job.backend = this;
	job.account = account;
	job.complete = false;
	job.result = B_OK;
	status_t status = PostJob(_CreateAccountJob, &job);
	if (status != B_OK) {
		delete account;
		return status;
	}

	std::unique_lock<std::mutex> guard(job.mutex);
	while (!job.complete)
		job.done.wait(guard);
	guard.unlock();

	if (job.result != B_OK) {
		delete account;
		return job.result;
	}

	{
		BAutolock lock(fLock);
		fAccounts[account->path] = account;
	}

	if (accountPath != NULL)
		*accountPath = account->path;

	BMessage notification(NOTIFICATION_MODEM_ADDED);
	notification.AddString(kMMFieldModemPath, account->path.String());
	notification.AddString(kMMFieldModemOperatorName, account->uri.String());
	notification.AddBool(kMMFieldModemEnabled, true);
	_NotifyWatchers(NOTIFICATION_MODEM_ADDED, notification);
	return B_OK;
}


status_t
SipBackend::RemoveAccount(const char* accountPath)
{
	if (accountPath == NULL)
		return B_BAD_VALUE;

	Account* account = NULL;
	{
		BAutolock lock(fLock);
		std::map<BString, Account*>::iterator it = fAccounts.find(
			accountPath);
		if (it == fAccounts.end())
			return B_ENTRY_NOT_FOUND;
		account = it->second;
		fAccounts.erase(it);
	}

	BMessage notification(NOTIFICATION_MODEM_REMOVED);
	notification.AddString(kMMFieldModemPath, account->path.String());
	_NotifyWatchers(NOTIFICATION_MODEM_REMOVED, notification);

	mem_deref(account->reg);
	delete account;
	return B_OK;
}


void
SipBackend::SetAudioHooks(SipAudioPullFunc pull, SipAudioPushFunc push,
	void* cookie)
{
	fAudioPull = pull;
	fAudioPush = push;
	fAudioCookie = cookie;
}


void
SipBackend::SetVideoHooks(SipVideoFrameFunc remoteFrame,
	SipVideoFrameFunc localFrame, void* cookie)
{
	fVideoRemote = remoteFrame;
	fVideoLocal = localFrame;
	fVideoCookie = cookie;
}


void
SipBackend::SetVideoCaptureDevice(const char* path)
{
	fVideoDevice = path != NULL ? path : "";
}


status_t
SipBackend::SendVideoPayload(const char* callPath, const uint8* payload,
	size_t size, bool keyFrame)
{
	Call* call = NULL;
	{
		BAutolock lock(fLock);
		std::map<BString, Call*>::iterator it = fCalls.find(callPath);
		if (it == fCalls.end())
			return B_ENTRY_NOT_FOUND;
		call = it->second;
	}

	if (!call->videoNegotiated || payload == NULL)
		return B_UNSUPPORTED;

	// RFC 7741 VP8 payload descriptor: the S bit marks a key frame
	// start; the rest of the frame rides as one packet here.
	struct mbuf* packet = mbuf_alloc(kRtpPresz + size + 2);
	if (packet == NULL)
		return B_NO_MEMORY;
	packet->pos = kRtpPresz;
	packet->end = kRtpPresz;
	mbuf_write_u8(packet, keyFrame ? 0x10 : 0x00);
	mbuf_write_mem(packet, payload, size);
	packet->pos = kRtpPresz;

	struct sa addr;
	sa_cpy(&addr, &call->rtpAddr);
	rtp_send(call->vrtp, &addr, false, keyFrame, call->videoPt,
		call->rtpTimestamp * 90 / 8, packet);
	mem_deref(packet);
	return B_OK;
}


status_t
SipBackend::SetVideoPayloadHandler(void (*handler)(void* cookie,
	const uint8* payload, size_t size, bool keyFrame), void* cookie)
{
	fVideoPayloadHandler = handler;
	fVideoPayloadCookie = cookie;
	return B_OK;
}


BString
SipBackend::VideoCallPath()
{
	BAutolock lock(fLock);
	for (std::map<BString, Call*>::iterator it = fCalls.begin();
			it != fCalls.end(); it++) {
		Call* call = it->second;
		if (call->state == kMMCallStateActive && call->videoNegotiated)
			return call->path;
	}
	return BString("");
}


// #pragma mark - TelephonyBackend: snapshots


status_t
SipBackend::GetModems(BMessage* outModems)
{
	if (outModems == NULL)
		return B_BAD_VALUE;

	BAutolock lock(fLock);
	int32 index = 0;
	outModems->AddInt32(kMMFieldModemCount, (int32)fAccounts.size());
	for (std::map<BString, Account*>::iterator it = fAccounts.begin();
			it != fAccounts.end(); it++, index++) {
		Account* account = it->second;
		BMessage info;
		info.AddString(kMMFieldModemPath, account->path.String());
		info.AddString(kMMFieldModemOperatorName, account->uri.String());
		info.AddUInt32(kMMFieldModemRegistrationState, account->regState);
		info.AddBool(kMMFieldModemEnabled, true);
		info.AddInt32(kMMFieldModemState, kMMStateEnabled);

		char name[32];
		snprintf(name, sizeof(name), "modem_%" B_PRId32, index);
		outModems->AddMessage(name, &info);
	}
	return B_OK;
}


status_t
SipBackend::GetModemInfo(const char* modemPath, BMessage* outInfo)
{
	if (modemPath == NULL || outInfo == NULL)
		return B_BAD_VALUE;

	BAutolock lock(fLock);
	std::map<BString, Account*>::iterator it = fAccounts.find(modemPath);
	if (it == fAccounts.end())
		return B_ENTRY_NOT_FOUND;

	Account* account = it->second;
	outInfo->AddString(kMMFieldModemPath, account->path.String());
	outInfo->AddString(kMMFieldModemOperatorName, account->uri.String());
	outInfo->AddString(kMMFieldModemManufacturer, "VitruvianOS");
	outInfo->AddString(kMMFieldModemModel, "libre SIP");
	outInfo->AddUInt32(kMMFieldModemRegistrationState, account->regState);
	outInfo->AddBool(kMMFieldModemEnabled, true);
	outInfo->AddInt32(kMMFieldModemState,
		account->registered ? kMMStateRegistered : kMMStateEnabled);

	const char* regName = "idle";
	if (account->regState == kMMRegHome)
		regName = "registered";
	else if (account->regState == kMMRegSearching)
		regName = "searching";
	else if (account->regState == kMMRegDenied)
		regName = "denied";
	outInfo->AddString(kMMFieldModemRegistrationName, regName);
	return B_OK;
}


static const char*
_StateName(uint32 state)
{
	switch (state) {
		case kMMCallStateDialing: return "dialing";
		case kMMCallStateRingingOut: return "ringing out";
		case kMMCallStateRingingIn: return "ringing in";
		case kMMCallStateActive: return "active";
		case kMMCallStateHeld: return "held";
		case kMMCallStateWaiting: return "waiting";
		case kMMCallStateTerminated: return "terminated";
		default: return "unknown";
	}
}


status_t
SipBackend::GetCalls(const char* modemPath, BMessage* outCalls)
{
	if (modemPath == NULL || outCalls == NULL)
		return B_BAD_VALUE;

	BAutolock lock(fLock);
	int32 index = 0;
	for (std::map<BString, Call*>::iterator it = fCalls.begin();
			it != fCalls.end(); it++) {
		Call* call = it->second;
		if (call->account->path != modemPath)
			continue;
		if (call->state == kMMCallStateTerminated)
			continue;

		BMessage info;
		info.AddString(kMMFieldCallPath, call->path.String());
		info.AddString(kMMFieldCallModemPath, call->account->path.String());
		info.AddString(kMMFieldCallNumber, call->peerUri.String());
		info.AddUInt32(kMMFieldCallState, call->state);
		info.AddString(kMMFieldCallStateName, _StateName(call->state));
		info.AddUInt32(kMMFieldCallDirection, call->direction);
		info.AddString(kMMFieldCallDirectionName,
			call->direction == kMMCallDirectionIncoming
				? "incoming" : "outgoing");
		if (call->dtmfLength > 0)
			info.AddString("dtmf_digits", call->lastDtmf);

		char name[32];
		snprintf(name, sizeof(name), "call_%" B_PRId32, index++);
		outCalls->AddMessage(name, &info);
	}
	outCalls->AddInt32(kMMFieldCallCount, index);
	return B_OK;
}


status_t
SipBackend::GetSmsMessages(const char* modemPath, BMessage* outMessages)
{
	if (modemPath == NULL || outMessages == NULL)
		return B_BAD_VALUE;

	BAutolock lock(fLock);
	int32 index = 0;
	for (size_t i = 0; i < fSmsLog.size(); i++) {
		BString owner;
		if (fSmsLog[i].FindString(kMMFieldSmsModemPath, &owner) != B_OK
				|| owner != modemPath)
			continue;

		char name[32];
		snprintf(name, sizeof(name), "sms_%" B_PRId32, index++);
		outMessages->AddMessage(name, &fSmsLog[i]);
	}
	outMessages->AddInt32(kMMFieldSmsCount, index);
	return B_OK;
}


// #pragma mark - TelephonyBackend: voice


status_t
SipBackend::DialAsync(const char* modemPath, const char* number,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (modemPath == NULL || number == NULL)
		return B_BAD_VALUE;

	Account* account = NULL;
	{
		BAutolock lock(fLock);
		std::map<BString, Account*>::iterator it = fAccounts.find(modemPath);
		if (it == fAccounts.end())
			return B_ENTRY_NOT_FOUND;
		account = it->second;
	}

	// A number becomes user@domain of the account's registrar; a
	// full SIP URI passes through.
	BString target;
	if (strncmp(number, "sip:", 4) == 0 || strncmp(number, "sips:", 5) == 0)
		target = number;
	else {
		const char* at = strchr(account->uri.String(), '@');
		target = "sip:";
		target << number;
		if (at != NULL)
			target << at;
	}

	DialJob job;
	job.backend = this;
	job.account = account;
	job.uri = target;
	job.complete = false;
	job.result = B_OK;

	status_t status = PostJob(_DialJob, &job);
	if (status != B_OK)
		return status;

	std::unique_lock<std::mutex> guard(job.mutex);
	while (!job.complete)
		job.done.wait(guard);
	guard.unlock();

	BMessage reply(replyWhat);
	reply.AddInt32("status", job.result);
	if (job.result == B_OK)
		reply.AddString(kMMFieldCallPath, job.callPath.String());
	replyTo.SendMessage(&reply);
	return job.result;
}


status_t
SipBackend::AnswerAsync(const char* callPath,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (callPath == NULL)
		return B_BAD_VALUE;
	if (_QueueCallOp(this, callPath, kOpAnswer, replyTo, replyWhat) == NULL)
		return B_ENTRY_NOT_FOUND;
	return B_OK;
}


status_t
SipBackend::HangupAsync(const char* modemPath, const char* callPath,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (callPath == NULL) {
		std::vector<BString> paths;
		{
			BAutolock lock(fLock);
			for (std::map<BString, Call*>::iterator it = fCalls.begin();
					it != fCalls.end(); it++) {
				if (modemPath != NULL
						&& it->second->account->path != modemPath)
					continue;
				if (it->second->state != kMMCallStateTerminated)
					paths.push_back(it->first);
			}
		}
		if (paths.empty())
			return B_ENTRY_NOT_FOUND;
		for (size_t i = 0; i < paths.size(); i++) {
			if (_QueueCallOp(this, paths[i].String(), kOpHangup,
					replyTo, replyWhat) == NULL)
				return B_ENTRY_NOT_FOUND;
		}
		return B_OK;
	}

	if (_QueueCallOp(this, callPath, kOpHangup, replyTo, replyWhat) == NULL)
		return B_ENTRY_NOT_FOUND;
	return B_OK;
}


status_t
SipBackend::HoldAsync(const char* modemPath,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (modemPath == NULL)
		return B_BAD_VALUE;

	BString activePath;
	{
		BAutolock lock(fLock);
		for (std::map<BString, Call*>::iterator it = fCalls.begin();
				it != fCalls.end(); it++) {
			Call* call = it->second;
			// Hold toggles: an Active call goes on hold, a Held
			// call comes back.
			if (call->account->path == modemPath
					&& (call->state == kMMCallStateActive
						|| call->state == kMMCallStateHeld)) {
				activePath = call->path;
				break;
			}
		}
	}
	if (activePath.IsEmpty())
		return B_ENTRY_NOT_FOUND;

	if (_QueueCallOp(this, activePath.String(), kOpHold, replyTo,
			replyWhat) == NULL)
		return B_ENTRY_NOT_FOUND;
	return B_OK;
}


status_t
SipBackend::SendDtmfAsync(const char* callPath, const char* digits,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (callPath == NULL || digits == NULL)
		return B_BAD_VALUE;

	CallOpJob* job = _QueueCallOp(this, callPath, kOpDtmf, replyTo,
		replyWhat);
	if (job == NULL)
		return B_ENTRY_NOT_FOUND;
	job->digits = digits;
	return B_OK;
}


// #pragma mark - TelephonyBackend: SMS


status_t
SipBackend::SendMessageAsync(const char* modemPath, const char* number,
	const char* text, const BMessenger& replyTo, uint32 replyWhat)
{
	if (modemPath == NULL || number == NULL || text == NULL)
		return B_BAD_VALUE;

	Account* account = NULL;
	{
		BAutolock lock(fLock);
		std::map<BString, Account*>::iterator it = fAccounts.find(modemPath);
		if (it == fAccounts.end())
			return B_ENTRY_NOT_FOUND;
		account = it->second;
	}

	// Reuse the call-op machinery; the call slot carries the
	// owning account for MESSAGE.
	CallOpJob* job = new CallOpJob();
	job->backend = this;
	job->call = (Call*)account;
	job->replyTo = replyTo;
	job->replyWhat = replyWhat;
	job->op = kOpSendMessage;
	job->number = number;
	job->text = text;
	job->result = B_OK;
	job->complete = false;
	if (PostJob(_CallOpJob, job) != B_OK) {
		delete job;
		return B_ERROR;
	}
	return B_OK;
}


status_t
SipBackend::DeleteSmsAsync(const char* modemPath, const char* smsPath,
	const BMessenger& replyTo, uint32 replyWhat)
{
	(void)modemPath;
	(void)smsPath;

	BMessage reply(replyWhat);
	reply.AddInt32("status", B_UNSUPPORTED);
	replyTo.SendMessage(&reply);
	return B_UNSUPPORTED;
}


// #pragma mark - TelephonyBackend: SIM stubs


status_t
SipBackend::EnableModemAsync(const char* modemPath, bool enabled,
	const BMessenger& replyTo, uint32 replyWhat)
{
	(void)modemPath;
	(void)enabled;

	BMessage reply(replyWhat);
	reply.AddInt32("status", B_OK);
	replyTo.SendMessage(&reply);
	return B_OK;
}


status_t
SipBackend::UnlockSimAsync(const char* modemPath, const char* pin,
	const BMessenger& replyTo, uint32 replyWhat)
{
	(void)modemPath;
	(void)pin;

	BMessage reply(replyWhat);
	reply.AddInt32("status", B_UNSUPPORTED);
	replyTo.SendMessage(&reply);
	return B_UNSUPPORTED;
}


status_t
SipBackend::UnlockSimPukAsync(const char* modemPath, const char* puk,
	const char* newPin, const BMessenger& replyTo, uint32 replyWhat)
{
	(void)modemPath;
	(void)puk;
	(void)newPin;

	BMessage reply(replyWhat);
	reply.AddInt32("status", B_UNSUPPORTED);
	replyTo.SendMessage(&reply);
	return B_UNSUPPORTED;
}


status_t
SipBackend::SetupSignalRateAsync(const char* modemPath, uint32 rateSeconds,
	const BMessenger& replyTo, uint32 replyWhat)
{
	(void)modemPath;
	(void)rateSeconds;

	BMessage reply(replyWhat);
	reply.AddInt32("status", B_OK);
	replyTo.SendMessage(&reply);
	return B_OK;
}


bool
SipBackend::IsServiceAvailable()
{
	return fSipStack != NULL || !fAccounts.empty();
}


// #pragma mark - Watching


status_t
SipBackend::StartWatching(const BMessenger& target, uint32 notificationMask)
{
	BAutolock lock(fLock);
	for (size_t i = 0; i < fWatchers.size(); i++) {
		if (fWatchers[i].messenger == target) {
			fWatchers[i].mask = notificationMask;
			return B_OK;
		}
	}

	Watcher watcher;
	watcher.messenger = target;
	watcher.mask = notificationMask;
	fWatchers.push_back(watcher);
	return B_OK;
}


status_t
SipBackend::StopWatching(const BMessenger& target)
{
	BAutolock lock(fLock);
	for (size_t i = 0; i < fWatchers.size(); i++) {
		if (fWatchers[i].messenger == target) {
			fWatchers.erase(fWatchers.begin() + i);
			return B_OK;
		}
	}
	return B_ENTRY_NOT_FOUND;
}


void
SipBackend::_NotifyWatchers(uint32 type, BMessage& message)
{
	BAutolock lock(fLock);
	for (size_t i = 0; i < fWatchers.size();) {
		Watcher& watcher = fWatchers[i];
		if (!watcher.messenger.IsValid()) {
			fWatchers.erase(fWatchers.begin() + i);
			continue;
		}
		if (watcher.mask == (uint32)~0
				|| (watcher.mask & (1u << (type & 0x3f))) != 0)
			watcher.messenger.SendMessage(&message);
		i++;
	}
}


// #pragma mark - Session control (loop thread)


void
SipBackend::_GatherIce(Call* call, bool offer)
{
	if (call->ice != NULL) {
		// Host candidates first; STUN adds the reflexive one.
		net_if_list([](const char* ifname, const struct sa* sa,
				void* arg) -> bool {
			struct icem* ice = (struct icem*)arg;
			if (sa_af(sa) != AF_INET || sa_is_loopback(sa))
				return false;
			icem_cand_add(ice, ICE_COMPID_RTP, 7000, ifname, sa);
			return false;
		}, call->ice);
		icem_conncheck_start(call->ice);
	}

	_StartStunGather(call, offer);
}


void
SipBackend::_SendOffer(Call* call)
{
	if (_BuildAudioSession(call, call->account->video) != 0) {
		call->state = kMMCallStateTerminated;
		_OnSessionClosed(ENOMEM, NULL, call);
		return;
	}

	struct mbuf* offer = NULL;
	if (_EncodeSdp(call, &offer, true) != 0) {
		call->state = kMMCallStateTerminated;
		_OnSessionClosed(EINVAL, NULL, call);
		return;
	}

	const char* routev[1] = { NULL };
	uint32_t routec = 0;
	if (!call->account->proxy.IsEmpty()) {
		routev[0] = call->account->proxy.String();
		routec = 1;
	}

	BString fromUri;
	fromUri << "sip:" << call->account->authUser.String();
	const char* at = strchr(call->account->uri.String(), '@');
	if (at != NULL)
		fromUri << at;

	int err = sipsess_connect((struct sipsess**)&call->sess,
		(struct sipsess_sock*)fSessionSock, call->peerUri.String(),
		call->account->displayName.String(), fromUri.String(),
		call->account->authUser.String(), routev, routec,
		"application/sdp", offer, &_OnAuthChallenge,
		call->account, true, &_OnSessionOffer, &_OnSessionAnswer,
		&_OnSessionProgress, &_OnSessionEstablished, NULL, NULL,
		&_OnSessionClosed, call, "%s", "");
	if (err != 0) {
		call->state = kMMCallStateTerminated;
		_OnSessionClosed(err, NULL, call);
	}
}


void
SipBackend::_ConnectSession(Call* call)
{
	_SetupRtp(call);

	if (call->account->ice) {
		char lufrag[9];
		char lpwd[25];
		rand_str(lufrag, sizeof(lufrag));
		rand_str(lpwd, sizeof(lpwd));
		icem_alloc(&call->ice, ICE_MODE_FULL, ICE_ROLE_CONTROLLING,
			IPPROTO_UDP, 0, rand_u64(), lufrag, lpwd, NULL, NULL);
		icem_comp_add(call->ice, ICE_COMPID_RTP,
			(void*)rtp_sock(call->rtp));
		call->iceControlling = true;
	}

	call->gathering = true;
	_GatherIce(call, true);
}


void
SipBackend::_SendAnswer(Call* call)
{
	if (call->sess == NULL || call->sdp == NULL)
		return;

	struct mbuf* answer = NULL;
	sdp_media_set_ldir(call->audioMedia, SDP_SENDRECV);
	if (_EncodeSdp(call, &answer, false) == 0) {
		sipsess_answer(call->sess, 200, "OK", answer, "%s", "");
		mem_deref(answer);
	}
	call->state = kMMCallStateActive;
	tmr_start(&call->audioTimer, 20, _AudioPump, call);

	BMessage notification(NOTIFICATION_CALL_STATE_CHANGED);
	notification.AddString(kMMFieldCallPath, call->path.String());
	notification.AddString(kMMFieldCallModemPath,
		call->account->path.String());
	notification.AddString(kMMFieldCallNumber, call->peerUri.String());
	notification.AddUInt32(kMMFieldCallState, kMMCallStateActive);
	notification.AddString(kMMFieldCallStateName, "active");
	notification.AddUInt32(kMMFieldCallDirection, call->direction);
	_NotifyWatchers(NOTIFICATION_CALL_STATE_CHANGED, notification);
}


void
SipBackend::_IncomingCall(const void* sipMsg)
{
	const struct sip_msg* msg = (const struct sip_msg*)sipMsg;
	Account* account = NULL;

	{
		BAutolock lock(fLock);
		for (std::map<BString, Account*>::iterator it = fAccounts.begin();
				it != fAccounts.end(); it++) {
			account = it->second;
			break;
		}
	}
	if (account == NULL) {
		sip_reply((struct sip*)fSipStack, msg, 480, "Temporarily "
			"Unavailable");
		return;
	}

	Call* call = new Call();
	call->backend = this;
	call->account = account;
	call->state = kMMCallStateRingingIn;
	call->direction = kMMCallDirectionIncoming;
	call->localHold = false;
	call->destroying = false;
	call->sess = NULL;
	call->sdp = NULL;
	call->audioMedia = NULL;
	call->videoMedia = NULL;
	call->rtp = NULL;
	call->vrtp = NULL;
	call->srtpSession = NULL;
	call->srtpHelper = NULL;
	call->telev = NULL;
	call->ice = NULL;
	call->iceControlling = false;
	call->stunMedia = NULL;
	call->stunSock = NULL;
	call->stunDone = true;
	call->pendingOffer = false;
	call->gathering = false;
	call->rtpTimestamp = 0;
	call->ssrc = rand_u32();
	call->audioPt = 0;
	call->televPt = 101;
	call->videoPt = 96;
	call->rtpAddrValid = false;
	call->audioNegotiated = false;
	call->videoNegotiated = false;
	call->dtmfLength = 0;
	call->lastDtmf[0] = '\0';
	call->srtpLocalKey = false;
	call->srtpActive = false;
	tmr_init(&call->audioTimer);
	tmr_init(&call->stunTimeout);

	char path[64];
	snprintf(path, sizeof(path), "sip-call:%d", fNextCallId++);
	call->path = path;

	char peer[256];
	snprintf(peer, sizeof(peer), "sip:%.*s@%.*s",
		(int)msg->from.uri.user.l, msg->from.uri.user.p,
		(int)msg->from.uri.host.l, msg->from.uri.host.p);
	call->peerUri = peer;

	{
		BAutolock lock(fLock);
		fCalls[call->path] = call;
	}

	// Decode the offer so Answer can encode the reply; the RTP
	// socket must exist before the answer carries its port.
	_SetupRtp(call);
	if (_BuildAudioSession(call, account->video) == 0
			&& mbuf_get_left(msg->mb) > 0) {
		struct mbuf* clone = mbuf_alloc(mbuf_get_left(msg->mb));
		if (clone != NULL) {
			mbuf_write_mem(clone, mbuf_buf(msg->mb),
				mbuf_get_left(msg->mb));
			clone->pos = 0;
			_DecodeRemoteSdp(call, clone, true);
			mem_deref(clone);
		}
	}

	if (account->ice) {
		char lufrag[9];
		char lpwd[25];
		rand_str(lufrag, sizeof(lufrag));
		rand_str(lpwd, sizeof(lpwd));
		icem_alloc(&call->ice, ICE_MODE_FULL, ICE_ROLE_CONTROLLED,
			IPPROTO_UDP, 0, rand_u64(), lufrag, lpwd, NULL, NULL);
		icem_comp_add(call->ice, ICE_COMPID_RTP,
			(void*)rtp_sock(call->rtp));
	}

	sipsess_accept((struct sipsess**)&call->sess,
		(struct sipsess_sock*)fSessionSock, msg, 180, "Ringing",
		account->authUser.String(), "application/sdp", NULL,
		NULL, NULL, false, &_OnSessionOffer,
		&_OnSessionAnswer, &_OnSessionEstablished, NULL, NULL,
		&_OnSessionClosed, call, "%s", "");

	BMessage added(NOTIFICATION_CALL_ADDED);
	added.AddString(kMMFieldCallPath, call->path.String());
	added.AddString(kMMFieldCallModemPath, account->path.String());
	added.AddString(kMMFieldCallNumber, call->peerUri.String());
	added.AddUInt32(kMMFieldCallState, kMMCallStateRingingIn);
	added.AddString(kMMFieldCallStateName, "ringing in");
	added.AddUInt32(kMMFieldCallDirection, kMMCallDirectionIncoming);
	_NotifyWatchers(NOTIFICATION_CALL_ADDED, added);
}


void
SipBackend::_AnswerCall(Call* call)
{
	if (call->sess == NULL || call->sdp == NULL)
		return;

	call->gathering = true;
	_GatherIce(call, false);
}


void
SipBackend::_HangupCall(Call* call)
{
	// Dereferencing the session sends BYE (or CANCEL while ringing).
	// The close handler may (or may not) fire synchronously and
	// destroy the call, so check the live map before touching it.
	BString path = call->path;
	struct sipsess* sess = call->sess;
	call->sess = NULL;
	if (sess != NULL)
		mem_deref(sess);

	Call* live = NULL;
	{
		BAutolock lock(fLock);
		std::map<BString, Call*>::iterator it = fCalls.find(path);
		if (it != fCalls.end())
			live = it->second;
	}
	if (live != NULL && live->state != kMMCallStateTerminated)
		_OnSessionClosed(0, NULL, live);
}


void
SipBackend::_HoldCall(Call* call)
{
	call->localHold = !call->localHold;
	if (call->sess != NULL && call->sdp != NULL) {
		sdp_media_set_ldir(call->audioMedia, call->localHold
			? SDP_SENDONLY : SDP_SENDRECV);
		struct mbuf* desc = NULL;
		if (_EncodeSdp(call, &desc, true) == 0) {
			sipsess_modify(call->sess, desc);
			mem_deref(desc);
		}
	}

	call->state = call->localHold ? kMMCallStateHeld : kMMCallStateActive;

	BMessage notification(NOTIFICATION_CALL_STATE_CHANGED);
	notification.AddString(kMMFieldCallPath, call->path.String());
	notification.AddString(kMMFieldCallModemPath,
		call->account->path.String());
	notification.AddString(kMMFieldCallNumber, call->peerUri.String());
	notification.AddUInt32(kMMFieldCallState, call->state);
	notification.AddString(kMMFieldCallStateName,
		_StateName(call->state));
	notification.AddUInt32(kMMFieldCallDirection, call->direction);
	_NotifyWatchers(NOTIFICATION_CALL_STATE_CHANGED, notification);
}


void
SipBackend::_SendDtmf(Call* call, const char* digits)
{
	if (call->telev == NULL)
		return;

	for (const char* p = digits; *p != '\0'; p++) {
		int event = telev_digit2code(*p);
		if (event < 0)
			continue;
		telev_send(call->telev, event, false);
		telev_send(call->telev, event, true);
	}
}


void
SipBackend::_DestroyCall(Call* call)
{
	if (call->destroying)
		return;
	call->destroying = true;

	tmr_cancel(&call->audioTimer);
	tmr_cancel(&call->stunTimeout);
	mem_deref(call->stunSock);
	call->stunSock = NULL;
	mem_deref(call->stunMedia);
	call->stunMedia = NULL;
	mem_deref(call->ice);
	call->ice = NULL;
	mem_deref(call->telev);
	call->telev = NULL;
	if (call->srtpHelper != NULL) {
		mem_deref(call->srtpHelper);
		call->srtpHelper = NULL;
	}
	mem_deref(call->srtpSession);
	call->srtpSession = NULL;
	mem_deref(call->vrtp);
	call->vrtp = NULL;
	mem_deref(call->rtp);
	call->rtp = NULL;
	mem_deref(call->sdp);
	call->sdp = NULL;

	struct sipsess* sess = call->sess;
	call->sess = NULL;
	mem_deref(sess);

	{
		BAutolock lock(fLock);
		fCalls.erase(call->path);
	}

	BMessage removed(NOTIFICATION_CALL_REMOVED);
	removed.AddString(kMMFieldCallPath, call->path.String());
	removed.AddString(kMMFieldCallModemPath, call->account->path.String());
	_NotifyWatchers(NOTIFICATION_CALL_REMOVED, removed);

	delete call;
}
