/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _TELEPHONY_SIP_BACKEND_H
#define _TELEPHONY_SIP_BACKEND_H


#include <Locker.h>
#include <Messenger.h>
#include <String.h>

#include <map>
#include <vector>

#include "TelephonyBackend.h"


// SIP account settings, flattened into a BMessage by the accounts kit
// and consumed by SipBackend::AddAccount(). The password never rides
// in the message: the accounts kit reads it from the keystore and
// hands it over separately.
static const char* const kSipFieldUri = "sip_uri";
static const char* const kSipFieldAuthUser = "sip_auth_user";
static const char* const kSipFieldDisplayName = "sip_display_name";
static const char* const kSipFieldOutboundProxy = "sip_outbound_proxy";
static const char* const kSipFieldStunServer = "sip_stun_server";
static const char* const kSipFieldIceEnabled = "sip_ice_enabled";
static const char* const kSipFieldTlsEnabled = "sip_tls_enabled";
static const char* const kSipFieldSrtpEnabled = "sip_srtp_enabled";
static const char* const kSipFieldVideoEnabled = "sip_video_enabled";


// SIP user-agent backend on libre; each account is a line alongside the modems
// in BTelephonyRoster. Audio crosses the seam as G.711 PCM through app hooks.
class SipBackend : public TelephonyBackend {
public:
	static SipBackend* Instance();

							SipBackend();
	virtual					~SipBackend();

	// Account management. settings carries the kSipField* keys;
	// password comes from the keystore via the accounts kit. The
	// line appears immediately; registration completes async and
	// arrives as NOTIFICATION_REGISTRATION_CHANGED.
	status_t				AddAccount(const BMessage& settings,
									const char* password,
									BString* accountPath = NULL);
	status_t				RemoveAccount(const char* accountPath);

	// Fixes the local SIP UDP/TLS port (0 = ephemeral); call
	// before AddAccount. The host test uses it to give each of
	// its two user agents a known port.
	void					SetSipPort(uint16 port) { fSipPort = port; }
	uint16					SipPort() { return fSipPort; }

	// Audio hooks: the backend pulls mic PCM with pull (returns
	// the samples it wants encoded and sent) and pushes far-end
	// PCM with push. G.711 and the RTP clock stay in the backend.
	typedef size_t			(*SipAudioPullFunc)(void* cookie,
									int16* samples, size_t maxSamples);
	typedef void			(*SipAudioPushFunc)(void* cookie,
									const int16* samples, size_t count);
	void					SetAudioHooks(SipAudioPullFunc pull,
									SipAudioPushFunc push, void* cookie);

	// Video hooks: I420 frames, local=true is the self view.
	typedef void			(*SipVideoFrameFunc)(void* cookie,
									const uint8* const* data,
									const int* stride, int width,
									int height, bool local);
	void					SetVideoHooks(SipVideoFrameFunc remoteFrame,
									SipVideoFrameFunc localFrame,
									void* cookie);
	// V4L2 device path for outgoing video ("" disables capture).
	void					SetVideoCaptureDevice(const char* path);

	// Raw VP8 in / out; the backend packetizes over RTP when the
	// call negotiated a video m-line.
	status_t				SendVideoPayload(const char* callPath,
									const uint8* payload, size_t size,
									bool keyFrame);
	status_t				SetVideoPayloadHandler(
									void (*handler)(void* cookie,
										const uint8* payload, size_t size,
										bool keyFrame),
									void* cookie);

	// Queues a func onto the shared libre loop thread; the free
	// job functions use it to marshal work off caller threads.
	status_t				PostJob(void (*func)(void*), void* arg);

	// TelephonyBackend
	virtual	status_t		GetModems(BMessage* outModems);
	virtual	status_t		GetModemInfo(const char* modemPath,
								BMessage* outInfo);
	virtual	status_t		GetCalls(const char* modemPath,
								BMessage* outCalls);
	virtual	status_t		GetSmsMessages(const char* modemPath,
								BMessage* outMessages);
	virtual	status_t		DialAsync(const char* modemPath,
								const char* number,
								const BMessenger& replyTo,
								uint32 replyWhat);
	virtual	status_t		AnswerAsync(const char* callPath,
								const BMessenger& replyTo,
								uint32 replyWhat);
	virtual	status_t		HangupAsync(const char* modemPath,
								const char* callPath,
								const BMessenger& replyTo,
								uint32 replyWhat);
	virtual	status_t		HoldAsync(const char* modemPath,
								const BMessenger& replyTo,
								uint32 replyWhat);
	virtual	status_t		SendDtmfAsync(const char* callPath,
								const char* digits,
								const BMessenger& replyTo,
								uint32 replyWhat);
	virtual	status_t		SendMessageAsync(const char* modemPath,
								const char* number,
								const char* text,
								const BMessenger& replyTo,
								uint32 replyWhat);
	virtual	status_t		DeleteSmsAsync(const char* modemPath,
								const char* smsPath,
								const BMessenger& replyTo,
								uint32 replyWhat);
	virtual	status_t		EnableModemAsync(const char* modemPath,
								bool enabled,
								const BMessenger& replyTo,
								uint32 replyWhat);
	virtual	status_t		UnlockSimAsync(const char* modemPath,
								const char* pin,
								const BMessenger& replyTo,
								uint32 replyWhat);
	virtual	status_t		UnlockSimPukAsync(const char* modemPath,
								const char* puk,
								const char* newPin,
								const BMessenger& replyTo,
								uint32 replyWhat);
	virtual	status_t		SetupSignalRateAsync(const char* modemPath,
								uint32 rateSeconds,
								const BMessenger& replyTo,
								uint32 replyWhat);
	virtual	bool			IsServiceAvailable();
	virtual	status_t		StartWatching(const BMessenger& target,
								uint32 notificationMask);
	virtual	status_t		StopWatching(const BMessenger& target);

	// Internal plumbing, public for the libre trampolines and
	// the host test.
	struct Account;
	struct Call;

	struct Watcher {
		BMessenger	messenger;
		uint32		mask;
	};

	void					_NotifyWatchers(uint32 type,
								BMessage& message);

	// libre loop thread only.
	void					_ConnectSession(Call* call);
	void					_AnswerCall(Call* call);
	void					_HangupCall(Call* call);
	void					_HoldCall(Call* call);
	void					_SendDtmf(Call* call, const char* digits);
	void					_DestroyCall(Call* call);
	void					_IncomingCall(const void* sipMsg);
	void					_SendOffer(Call* call);
	void					_SendAnswer(Call* call);
	void					_GatherIce(Call* call, bool offer);
	void					_StartStunGather(Call* call, bool offer);
	void					_OnStunGathered(Call* call, bool offer,
									const void* mappedSa);
	void					_OnCallEstablished(Call* call,
									const void* sipMsg);
	void					_DecodeCallSdp(Call* call, const void* sipMsg,
									bool offer);
	void					_SendMessage(Call* account, const char* number,
									const char* text);

			BLocker			fLock;
			std::vector<Watcher>	fWatchers;

			std::map<BString, Account*>	fAccounts;
			std::map<BString, Call*>	fCalls;
			std::vector<BMessage>	fSmsLog;

			void*			fSipStack;
			void*			fSessionSock;
			void*			fMessageLsnr;
			void*			fTlsConfig;
			uint16			fSipPort;
			int32			fNextCallId;

			SipAudioPullFunc	fAudioPull;
			SipAudioPushFunc	fAudioPush;
			void*			fAudioCookie;
			SipVideoFrameFunc	fVideoRemote;
			SipVideoFrameFunc	fVideoLocal;
			void*			fVideoCookie;
			BString			fVideoDevice;
			void			(*fVideoPayloadHandler)(void* cookie,
									const uint8* payload, size_t size,
									bool keyFrame);
			void*			fVideoPayloadCookie;
};


#endif // _TELEPHONY_SIP_BACKEND_H
