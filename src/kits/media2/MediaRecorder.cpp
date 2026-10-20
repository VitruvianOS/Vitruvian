/*
 * Copyright 2025-2026, Dario Casalinuovo. All Rights Reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <media2/MediaRecorder.h>

#include <new>
#include <string.h>

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

#include <Entry.h>
#include <File.h>
#include <ObjectList.h>
#include <Path.h>
#include <syscalls.h>

#include <media2/MediaClient.h>
#include <media2/MediaConnection.h>
#include <media2/MediaGraph.h>
#include <media2/SimpleMediaNode.h>

#include "GStreamerBackend.h"


namespace {


typedef void (*_DeliverFunc)(void* cookie, void* buffer, size_t size,
	const BMediaFormat& format);


class _BridgeInput : public BMediaInput {
public:
	_BridgeInput(const char* name, media_type_mask accepted,
			_DeliverFunc deliver, void* cookie)
		:
		BMediaConnection(B_MEDIA_INPUT, name),
		BMediaInput(name),
		fDeliver(deliver),
		fCookie(cookie)
	{
		SetAcceptedTypes(accepted);
	}
	virtual ~_BridgeInput() {}

protected:
	virtual status_t AcceptFormat(BMediaFormat* format) override
	{
		if (format == NULL)
			return B_BAD_VALUE;
		if ((AcceptedTypes() & B_MEDIA_TYPE_BIT(format->Type())) == 0)
			return B_BAD_VALUE;
		return B_OK;
	}

	virtual void HandleBuffer(void* buffer, size_t size,
		const BMediaFormat& format) override
	{
		if (fDeliver != NULL)
			fDeliver(fCookie, buffer, size, format);
	}

private:
	_DeliverFunc			fDeliver;
	void*					fCookie;
};


// Video recorders own an in-process client with a video and an audio input.
class _RecorderClient : public BMediaClient {
public:
	_RecorderClient(const char* name, _DeliverFunc videoDeliver,
			void* videoCookie, _DeliverFunc audioDeliver, void* audioCookie)
		:
		BMediaClient(name != NULL ? name : "BMediaRecorder",
			B_MEDIA_RECORDER),
		fVideoInput(NULL),
		fAudioInput(NULL)
	{
		fVideoInput = new(std::nothrow) _BridgeInput("video",
			B_MEDIA_TYPE_BIT(B_MEDIA_RAW_VIDEO), videoDeliver, videoCookie);
		fAudioInput = new(std::nothrow) _BridgeInput("audio",
			B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO), audioDeliver, audioCookie);
		if (fVideoInput == NULL || fAudioInput == NULL)
			return;
		RegisterInput(fVideoInput);
		RegisterInput(fAudioInput);
	}

	virtual ~_RecorderClient()
	{
		if (fVideoInput != NULL) {
			UnregisterInput(fVideoInput);
			delete fVideoInput;
		}
		if (fAudioInput != NULL) {
			UnregisterInput(fAudioInput);
			delete fAudioInput;
		}
	}

	virtual	bool IsInProcessRouted() const { return true; }

			_BridgeInput*	VideoInput() const { return fVideoInput; }
			_BridgeInput*	AudioInput() const { return fAudioInput; }

protected:
	virtual	status_t _StartConnections(void* /*backend*/) { return B_OK; }

private:
			_BridgeInput*	fVideoInput;
			_BridgeInput*	fAudioInput;
};


} // anonymous namespace


class BMediaRecorder::Impl {
public:
	Impl(BMediaRecorder* owner, const char* name, media_type type);
	~Impl();

	status_t InitCheck() const { return fInitErr; }

	const BMediaFormat& Format() const { return fFormat; }
	status_t SetFormat(const BMediaFormat& format);
	status_t SetAudioFormat(const BMediaFormat& format);
	status_t SetAudioSource(media_client_id device);
	bool     HasAudioSource() const { return fMicLinked; }

	BMediaInput* Input() const { return fInput; }
	BMediaInput* VideoInput() const
		{ return fClient != NULL ? (BMediaInput*)fClient->VideoInput() : NULL; }
	BMediaInput* AudioInput() const
		{ return fClient != NULL ? (BMediaInput*)fClient->AudioInput() : NULL; }
	BMediaClient* Client() const { return fClient; }

	status_t Start();
	void     Stop();
	bool     IsRecording() const;

	void SetCallbacks(BMediaRecorder::RecordBuffer recordFunc,
		BMediaRecorder::Notifier notifyFunc, void* cookie);
	void SetBufferHooks(BMediaRecorder::BufferFunc bufferFunc,
		BMediaRecorder::Notifier notifyFunc, void* cookie);

	size_t BufferSize() const { return fFormat.format.u.raw_audio.buffer_size; }

	status_t SetOutputFile(const char* path,
		const BMediaFormat& encodedFormat, uint32 sampleRate,
		uint32 channelCount, BMediaRecorder::sample_format sampleFormat,
		float quality);
	status_t SetOutputFile(const char* path,
		const BMediaFormat& encodedVideoFormat,
		const BMediaFormat& encodedAudioFormat);
	void     ClearOutputFile();

private:
	static void _VideoThunk(void* cookie, void* buffer, size_t size,
		const BMediaFormat& format);
	static void _AudioThunk(void* cookie, void* buffer, size_t size,
		const BMediaFormat& format);
	void _DeliverVideo(void* buffer, size_t size, const BMediaFormat& format);
	void _DeliverAudio(void* buffer, size_t size, const BMediaFormat& format);
	void _PushAudio(void* buffer, size_t size, uint32 frameCount,
		const BMediaFormat& format);
	status_t _StartMic();
	void _StopMic();
	static void _MicRecord(void* cookie, bigtime_t timestamp, void* data,
		size_t size, const media_raw_audio_format& format);
	void _SetVideoCaps(const BMediaFormat& format);
	void _SetAudioCaps(const BMediaFormat& format);

	static void _ProcessHook(void* cookie, void* buffer, size_t size,
		uint32 frameCount, const BMediaFormat& format);

	BMediaRecorder*	fOwner;
	BMediaFormat	fFormat;
	BMediaFormat	fAudioFormat;
	BSimpleMediaNode*		fNode;
	_RecorderClient*		fClient;
	BMediaInput*		fInput;
	status_t				fInitErr;

	// Audio leg owned by the recorder, see SetAudioSource().
	BMediaRecorder*			fMic;
	bool					fMicWanted;
	media_client_id			fMicDevice;
	BMediaGraph::ForeignPort	fMicSource;
	bool					fMicLinked;

	BMediaRecorder::BufferFunc		fBufferFunc;
	BMediaRecorder::RecordBuffer	fRecordFunc;
	BMediaRecorder::Notifier		fNotifyFunc;
	void*							fCookie;

	// Encode-to-file
	GstElement*			fEncodePipeline;
	GstElement*			fEncodeAppSrc;	// borrowed (owned by pipeline)
	GstElement*			fEncodeVideoSrc;	// muxed A/V only, borrowed
	GstElement*			fEncodeAudioSrc;
	bool				fVideoCapsSet;
	bool				fAudioCapsSet;
	uint64				fVideoPts;	// ns
	uint64				fAudioPts;	// ns
	uint32				fFrameStride;
	bool				fCapsWarned = false;
	bool				fPushWarned = false;
	uint64				fEncodeNextPts;	// ns
	uint32				fEncodeSampleRate;
	uint32				fEncodeChannels;
	const char*			fEncodeFormatName;
	float				fEncodeQuality;
	bool				fQualityApplied;
	int					fQualityTries;
};


BMediaRecorder::Impl::Impl(BMediaRecorder* owner, const char* name,
	media_type type)
	:
	fOwner(owner),
	fFormat(),
	fAudioFormat(),
	fNode(NULL),
	fClient(NULL),
	fInput(NULL),
	fInitErr(B_NO_INIT),
	fMic(NULL),
	fMicWanted(false),
	fMicDevice(0),
	fMicLinked(false),
	fBufferFunc(NULL),
	fRecordFunc(NULL),
	fNotifyFunc(NULL),
	fCookie(NULL),
	fEncodePipeline(NULL),
	fEncodeAppSrc(NULL),
	fEncodeVideoSrc(NULL),
	fEncodeAudioSrc(NULL),
	fVideoCapsSet(false),
	fAudioCapsSet(false),
	fVideoPts(0),
	fAudioPts(0),
	fFrameStride(0),
	fEncodeNextPts(0),
	fEncodeSampleRate(0),
	fEncodeChannels(0),
	fEncodeFormatName(NULL),
	fEncodeQuality(-1.0f),
	fQualityApplied(false),
	fQualityTries(0)
{
	fFormat.SetToDefault();

	if (type == B_MEDIA_RAW_VIDEO) {
		fClient = new(std::nothrow) _RecorderClient(
			name != NULL ? name : "BMediaRecorder",
			&Impl::_VideoThunk, this, &Impl::_AudioThunk, this);
		if (fClient == NULL || fClient->VideoInput() == NULL
			|| fClient->AudioInput() == NULL
			|| fClient->InitCheck() != B_OK) {
			delete fClient;
			fClient = NULL;
			return;
		}
		fInitErr = B_OK;
		return;
	}

	fNode = new(std::nothrow) BSimpleMediaNode(
		name != NULL ? name : "BMediaRecorder",
		type, B_MEDIA_RECORDER);
	if (fNode == NULL) {
		fInitErr = B_NO_MEMORY;
		return;
	}
	if (fNode->InitCheck() != B_OK) {
		fInitErr = fNode->InitCheck();
		return;
	}
	fNode->SetFormat(fFormat);
	fNode->SetProcessHook(&Impl::_ProcessHook, this);

	fInput = new(std::nothrow) BMediaInput("BMediaRecorder-in");
	if (fInput == NULL) {
		fInitErr = B_NO_MEMORY;
		return;
	}
	if (fNode->RegisterInput(fInput) != B_OK) {
		fInitErr = B_ERROR;
		return;
	}
	fInitErr = B_OK;
}


BMediaRecorder::Impl::~Impl()
{
	if (fNode != NULL) {
		fNode->Stop();
		if (fInput != NULL)
			fNode->UnregisterInput(fInput);
	}
	if (fClient != NULL)
		fClient->Stop();
	_StopMic();
	delete fMic;
	ClearOutputFile();
	delete fInput;
	delete fNode;
	delete fClient;
}


status_t
BMediaRecorder::Impl::SetFormat(const BMediaFormat& fmt)
{
	if (fNode == NULL && fClient == NULL)
		return B_NO_INIT;
	if (IsRecording())
		return B_NOT_ALLOWED;
	fFormat = fmt;
	if (fNode != NULL)
		return fNode->SetFormat(fmt);
	fClient->VideoInput()->SetAcceptedFormat(fmt);
	return B_OK;
}


status_t
BMediaRecorder::Impl::SetAudioFormat(const BMediaFormat& fmt)
{
	if (fClient == NULL)
		return B_NOT_SUPPORTED;
	if (IsRecording())
		return B_NOT_ALLOWED;
	fAudioFormat = fmt;
	fClient->AudioInput()->SetAcceptedFormat(fmt);
	return B_OK;
}


status_t
BMediaRecorder::Impl::SetAudioSource(media_client_id device)
{
	if (fClient == NULL)
		return B_NOT_SUPPORTED;
	if (IsRecording())
		return B_NOT_ALLOWED;
	fMicWanted = true;
	fMicDevice = device;
	return B_OK;
}


status_t
BMediaRecorder::Impl::_StartMic()
{
	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	media_client_id device = fMicDevice;
	if (device == 0) {
		status_t status = graph->GetDefaultAudioInput(&device);
		if (status != B_OK)
			return status;
	}

	BObjectList<BMediaGraph::ForeignPort, true> ports;
	status_t status = graph->GetForeignPorts((uint32)device, &ports);
	if (status != B_OK)
		return status;
	BMediaGraph::ForeignPort* source = NULL;
	for (int32 i = 0; i < ports.CountItems(); i++) {
		if (ports.ItemAt(i)->direction == "out") {
			source = ports.ItemAt(i);
			break;
		}
	}
	if (source == NULL)
		return B_ENTRY_NOT_FOUND;
	fMicSource = *source;

	if (fMic == NULL) {
		fMic = new(std::nothrow) BMediaRecorder("recorder-mic",
			B_MEDIA_RAW_AUDIO);
		if (fMic == NULL || fMic->InitCheck() != B_OK) {
			delete fMic;
			fMic = NULL;
			return B_NO_MEMORY;
		}
		BMediaFormat format;
		format.SetToDefault();
		format.format.type = B_MEDIA_RAW_AUDIO;
		media_raw_audio_format& raw = format.format.u.raw_audio;
		raw.frame_rate = 48000.0f;
		raw.channel_count = 2;
		raw.format = media_raw_audio_format::B_AUDIO_SHORT;
		raw.byte_order = B_MEDIA_HOST_ENDIAN;
		raw.buffer_size = 4096;
		fMic->SetFormat(format);
	}

	// The capture stream must exist before the server can link it.
	fMic->SetHooks(&Impl::_MicRecord, NULL, this);
	status = fMic->Start();
	if (status == B_OK)
		status = graph->ConnectChannels(fMicSource.nodeId, fMic->Input());
	if (status != B_OK) {
		fMic->Stop(true);
		fMic->SetHooks(NULL, NULL, NULL);
		return status;
	}
	fMicLinked = true;
	return B_OK;
}


void
BMediaRecorder::Impl::_StopMic()
{
	if (fMic == NULL || !fMicLinked)
		return;
	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph != NULL)
		graph->Disconnect(&fMicSource, fMic->Input());
	fMicLinked = false;
	fMic->Stop(true);
	fMic->SetHooks(NULL, NULL, NULL);
}


/* static */ void
BMediaRecorder::Impl::_MicRecord(void* cookie, bigtime_t, void* data,
	size_t size, const media_raw_audio_format& raw)
{
	Impl* self = (Impl*)cookie;
	BMediaFormat format;
	format.SetToDefault();
	format.format.type = B_MEDIA_RAW_AUDIO;
	format.format.u.raw_audio = raw;
	self->_DeliverAudio(data, size, format);
}


status_t
BMediaRecorder::Impl::Start()
{
	if (fNode == NULL && fClient == NULL)
		return B_NO_INIT;
	status_t err = fClient != NULL ? fClient->Start() : fNode->Start();
	if (err != B_OK)
		return err;

	if (fClient != NULL && fMicWanted && !fMicLinked)
		_StartMic();	// failure leaves a video-only file

	// A leg nobody feeds must not hold the muxer back.
	if (fClient != NULL && fEncodePipeline != NULL) {
		if (!fClient->AudioInput()->IsConnected() && !fMicLinked
			&& fEncodeAudioSrc != NULL)
			gst_app_src_end_of_stream(GST_APP_SRC(fEncodeAudioSrc));
		if (!fClient->VideoInput()->IsConnected() && fEncodeVideoSrc != NULL)
			gst_app_src_end_of_stream(GST_APP_SRC(fEncodeVideoSrc));
	}
	if (fNotifyFunc != NULL)
		fNotifyFunc(fCookie, BMediaRecorder::B_STARTED);
	return B_OK;
}


void
BMediaRecorder::Impl::Stop()
{
	if (fNode == NULL && fClient == NULL)
		return;
	_StopMic();
	if (fClient != NULL) {
		if (fClient->IsStarted())
			fClient->Stop();
	} else if (fNode->IsStarted())
		fNode->Stop();
	if (fEncodePipeline != NULL
		&& (fFrameStride > 0 || fEncodeVideoSrc != NULL)) {
		if (fEncodeVideoSrc != NULL)
			gst_app_src_end_of_stream(GST_APP_SRC(fEncodeVideoSrc));
		if (fEncodeAudioSrc != NULL)
			gst_app_src_end_of_stream(GST_APP_SRC(fEncodeAudioSrc));
		if (fEncodeAppSrc != NULL)
			gst_app_src_end_of_stream(GST_APP_SRC(fEncodeAppSrc));
		GstBus* bus = gst_element_get_bus(fEncodePipeline);
		if (bus != NULL) {
			GstMessage* msg = gst_bus_timed_pop_filtered(bus, 5 * GST_SECOND,
				(GstMessageType)(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
			if (msg == NULL) {
				fprintf(stderr, "MediaRecorder: no EOS within 5s\n");
			} else if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
				GError* e = NULL;
				gst_message_parse_error(msg, &e, NULL);
				fprintf(stderr, "MediaRecorder: encode error: %s\n",
					e != NULL ? e->message : "?");
				if (e != NULL)
					g_error_free(e);
				gst_message_unref(msg);
			} else {
				fprintf(stderr, "MediaRecorder: encode EOS\n");
				gst_message_unref(msg);
			}
			gst_object_unref(bus);
		}
		gst_element_set_state(fEncodePipeline, GST_STATE_NULL);
	}
	if (fNotifyFunc != NULL)
		fNotifyFunc(fCookie, BMediaRecorder::B_STOPPED);
}


status_t
BMediaRecorder::Impl::SetOutputFile(const char* path,
	const BMediaFormat& encodedFormat, uint32 sampleRate,
	uint32 channelCount, BMediaRecorder::sample_format sampleFormat,
	float quality)
{
	if (path == NULL)
		return B_BAD_VALUE;
	if (fNode == NULL)
		return B_NOT_SUPPORTED;
	if (IsRecording())
		return B_NOT_ALLOWED;

	using namespace BPrivate::media;
	GStreamerBackend* gst = GStreamerBackend::GetInstance();
	if (gst == NULL)
		return B_NO_INIT;

	ClearOutputFile();

	const char* formatName = NULL;
	switch (sampleFormat) {
		case BMediaRecorder::B_SAMPLE_FORMAT_S16: formatName = "S16LE"; break;
		case BMediaRecorder::B_SAMPLE_FORMAT_S24: formatName = "S24LE"; break;
		case BMediaRecorder::B_SAMPLE_FORMAT_S32: formatName = "S32LE"; break;
		case BMediaRecorder::B_SAMPLE_FORMAT_F32: formatName = "F32LE"; break;
		default: break;
	}

	fEncodePipeline = gst->CreateEncodePipeline(path, encodedFormat,
		sampleRate, channelCount, formatName);
	if (fEncodePipeline == NULL)
		return B_ERROR;

	fEncodeAppSrc = gst_bin_get_by_name(GST_BIN(fEncodePipeline), "src");
	if (fEncodeAppSrc == NULL) {
		gst_element_set_state(fEncodePipeline, GST_STATE_NULL);
		gst_object_unref(fEncodePipeline);
		fEncodePipeline = NULL;
		return B_ERROR;
	}
	gst_object_unref(fEncodeAppSrc);	// borrowed; bin owns

	// Caps come from the first real buffer; the format is not known yet.
	g_object_set(fEncodeAppSrc, "format", GST_FORMAT_TIME, "is-live", TRUE,
		NULL);
	fFrameStride = 0;
	fEncodeNextPts = 0;
	fEncodeSampleRate = sampleRate;
	fEncodeChannels = channelCount;
	fEncodeFormatName = formatName;
	fEncodeQuality = quality;
	fQualityApplied = false;
	fQualityTries = 0;

	if (gst_element_set_state(fEncodePipeline, GST_STATE_PLAYING)
			== GST_STATE_CHANGE_FAILURE) {
		ClearOutputFile();
		return B_ERROR;
	}
	return B_OK;
}


status_t
BMediaRecorder::Impl::SetOutputFile(const char* path,
	const BMediaFormat& encodedVideoFormat,
	const BMediaFormat& encodedAudioFormat)
{
	if (path == NULL || fClient == NULL)
		return B_BAD_VALUE;
	if (!fFormat.IsRawVideo())
		return B_NOT_ALLOWED;
	if (IsRecording())
		return B_NOT_ALLOWED;

	using namespace BPrivate::media;
	GStreamerBackend* gst = GStreamerBackend::GetInstance();
	if (gst == NULL)
		return B_NO_INIT;

	ClearOutputFile();

	fEncodePipeline = gst->CreateAVEncodePipeline(path, &encodedVideoFormat,
		&encodedAudioFormat);
	if (fEncodePipeline == NULL)
		return B_ERROR;

	fEncodeVideoSrc = gst_bin_get_by_name(GST_BIN(fEncodePipeline), "vsrc");
	fEncodeAudioSrc = gst_bin_get_by_name(GST_BIN(fEncodePipeline), "asrc");
	if (fEncodeVideoSrc == NULL || fEncodeAudioSrc == NULL) {
		if (fEncodeVideoSrc != NULL)
			gst_object_unref(fEncodeVideoSrc);
		if (fEncodeAudioSrc != NULL)
			gst_object_unref(fEncodeAudioSrc);
		fEncodeVideoSrc = NULL;
		fEncodeAudioSrc = NULL;
		ClearOutputFile();
		return B_ERROR;
	}
	// Borrowed; the bin owns them.
	gst_object_unref(fEncodeVideoSrc);
	gst_object_unref(fEncodeAudioSrc);
	fVideoCapsSet = false;
	fAudioCapsSet = false;
	fVideoPts = 0;
	fAudioPts = 0;

	if (gst_element_set_state(fEncodePipeline, GST_STATE_PLAYING)
			== GST_STATE_CHANGE_FAILURE) {
		ClearOutputFile();
		return B_ERROR;
	}
	return B_OK;
}


bool
BMediaRecorder::Impl::IsRecording() const
{
	if (fClient != NULL)
		return fClient->IsStarted();
	return fNode != NULL && fNode->IsStarted();
}


void
BMediaRecorder::Impl::ClearOutputFile()
{
	if (fEncodePipeline == NULL)
		return;
	gst_element_set_state(fEncodePipeline, GST_STATE_NULL);
	gst_object_unref(fEncodePipeline);
	fEncodePipeline = NULL;
	fEncodeAppSrc   = NULL;
	fEncodeVideoSrc = NULL;
	fEncodeAudioSrc = NULL;
	fVideoCapsSet = false;
	fAudioCapsSet = false;
	fEncodeSampleRate = 0;
	fEncodeChannels = 0;
	fEncodeFormatName = NULL;
	fEncodeQuality = -1.0f;
	fQualityApplied = false;
	fQualityTries = 0;
}


void
BMediaRecorder::Impl::SetCallbacks(BMediaRecorder::RecordBuffer recordFunc,
	BMediaRecorder::Notifier notifyFunc, void* cookie)
{
	fRecordFunc = recordFunc;
	fNotifyFunc = notifyFunc;
	fCookie     = cookie;
}


void
BMediaRecorder::Impl::SetBufferHooks(BMediaRecorder::BufferFunc bufferFunc,
	BMediaRecorder::Notifier notifyFunc, void* cookie)
{
	fBufferFunc = bufferFunc;
	fNotifyFunc = notifyFunc;
	fCookie     = cookie;
}


void
BMediaRecorder::Impl::_SetVideoCaps(const BMediaFormat& format)
{
	if (fEncodeVideoSrc == NULL || fVideoCapsSet || !format.IsRawVideo())
		return;

	const media_raw_video_format& raw = format.format.u.raw_video;
	int numerator = 0;
	int denominator = 0;
	gst_util_double_to_fraction(raw.field_rate, &numerator, &denominator);
	if (numerator <= 0 || denominator <= 0) {
		numerator = 30;
		denominator = 1;
	}

	GstCaps* caps = gst_caps_new_simple("video/x-raw",
		"format", G_TYPE_STRING, "BGRx",
		"width", G_TYPE_INT, (int)raw.width,
		"height", G_TYPE_INT, (int)raw.height,
		"framerate", GST_TYPE_FRACTION, numerator, denominator,
		NULL);
	g_object_set(fEncodeVideoSrc, "caps", caps, "format", GST_FORMAT_TIME,
		"is-live", TRUE, NULL);
	gst_caps_unref(caps);
	fVideoCapsSet = true;
}


void
BMediaRecorder::Impl::_SetAudioCaps(const BMediaFormat& format)
{
	if (fEncodeAudioSrc == NULL || fAudioCapsSet || !format.IsRawAudio())
		return;

	GstCaps* caps
		= BPrivate::media::GStreamerBackend::RawAudioToGstCaps(format);
	if (caps == NULL)
		return;
	g_object_set(fEncodeAudioSrc, "caps", caps, "format", GST_FORMAT_TIME,
		"is-live", TRUE, NULL);
	gst_caps_unref(caps);
	fAudioCapsSet = true;
}


/* static */ void
BMediaRecorder::Impl::_VideoThunk(void* cookie, void* buffer, size_t size,
	const BMediaFormat& format)
{
	((Impl*)cookie)->_DeliverVideo(buffer, size, format);
}


/* static */ void
BMediaRecorder::Impl::_AudioThunk(void* cookie, void* buffer, size_t size,
	const BMediaFormat& format)
{
	((Impl*)cookie)->_DeliverAudio(buffer, size, format);
}


void
BMediaRecorder::Impl::_DeliverVideo(void* buffer, size_t size,
	const BMediaFormat& format)
{
	if (fBufferFunc != NULL && buffer != NULL && size > 0)
		fBufferFunc(fCookie, 0, buffer, size, format);

	if (fEncodePipeline == NULL || fEncodeVideoSrc == NULL || buffer == NULL
		|| size == 0) {
		return;
	}

	if (!fVideoCapsSet)
		_SetVideoCaps(fFormat.IsRawVideo() ? fFormat : format);
	if (!fVideoCapsSet)
		return;

	GstBuffer* gstBuffer = gst_buffer_new_allocate(NULL, size, NULL);
	if (gstBuffer == NULL)
		return;
	GstMapInfo map;
	if (!gst_buffer_map(gstBuffer, &map, GST_MAP_WRITE)) {
		gst_buffer_unref(gstBuffer);
		return;
	}
	memcpy(map.data, buffer, size);
	gst_buffer_unmap(gstBuffer, &map);

	const float rate = format.IsRawVideo()
		? format.format.u.raw_video.field_rate : 0.0f;
	if (rate > 0.0f) {
		const uint64 duration = (uint64)((double)GST_SECOND / (double)rate);
		GST_BUFFER_PTS(gstBuffer) = fVideoPts;
		GST_BUFFER_DURATION(gstBuffer) = duration;
		fVideoPts += duration;
	}

	if (gst_app_src_push_buffer(GST_APP_SRC(fEncodeVideoSrc), gstBuffer)
			!= GST_FLOW_OK) {
		gst_buffer_unref(gstBuffer);
	}
}


void
BMediaRecorder::Impl::_DeliverAudio(void* buffer, size_t size,
	const BMediaFormat& format)
{
	if (fBufferFunc != NULL && buffer != NULL && size > 0)
		fBufferFunc(fCookie, 0, buffer, size, format);

	if (fEncodePipeline == NULL || fEncodeAudioSrc == NULL)
		return;

	const BMediaFormat& caps = fAudioFormat.IsRawAudio() ? fAudioFormat
		: format;
	if (!fAudioCapsSet)
		_SetAudioCaps(caps);
	_PushAudio(buffer, size, 0, caps);
}


void
BMediaRecorder::Impl::_PushAudio(void* buffer, size_t size,
	uint32 frameCount, const BMediaFormat& format)
{
	if (fEncodePipeline == NULL || fEncodeAudioSrc == NULL || buffer == NULL
		|| !fAudioCapsSet || !format.IsRawAudio()) {
		return;
	}

	const media_raw_audio_format& raw = format.format.u.raw_audio;
	const uint32 stride
		= (raw.format & media_raw_audio_format::B_AUDIO_SIZE_MASK)
			* raw.channel_count;
	if (stride == 0)
		return;
	if (frameCount == 0)
		frameCount = size / stride;

	GstBuffer* gstBuffer = gst_buffer_new_allocate(NULL, size, NULL);
	if (gstBuffer == NULL)
		return;
	GstMapInfo map;
	if (!gst_buffer_map(gstBuffer, &map, GST_MAP_WRITE)) {
		gst_buffer_unref(gstBuffer);
		return;
	}
	memcpy(map.data, buffer, size);
	gst_buffer_unmap(gstBuffer, &map);

	if (raw.frame_rate > 0.0f) {
		const uint64 duration = (frameCount * (uint64)GST_SECOND)
			/ (uint64)raw.frame_rate;
		GST_BUFFER_PTS(gstBuffer) = fAudioPts;
		GST_BUFFER_DURATION(gstBuffer) = duration;
		fAudioPts += duration;
	}

	if (gst_app_src_push_buffer(GST_APP_SRC(fEncodeAudioSrc), gstBuffer)
			!= GST_FLOW_OK) {
		gst_buffer_unref(gstBuffer);
	}
}


void
BMediaRecorder::Impl::_ProcessHook(void* cookie, void* buffer, size_t size,
	uint32 frameCount, const BMediaFormat& format)
{
	Impl* self = (Impl*)cookie;

	if (self->fBufferFunc != NULL) {
		self->fBufferFunc(self->fCookie, 0, buffer, size, format);
	} else if (self->fRecordFunc != NULL) {
		// Legacy ProcessFunc signature: (cookie, timestamp, data, size, format)
		self->fRecordFunc(self->fCookie, /*timestamp*/ 0, buffer, size,
			format.format.u.raw_audio);
	} else if (self->fOwner != NULL) {
		self->fOwner->BufferReceived(buffer, size, format.format.u.raw_audio);
	}

	if (self->fEncodePipeline != NULL && self->fEncodeAppSrc != NULL
			&& self->fFrameStride == 0) {
		const media_raw_audio_format& raw = format.format.u.raw_audio;
		GstCaps* inCaps
			= BPrivate::media::GStreamerBackend::RawAudioToGstCaps(format);
		if (inCaps != NULL) {
			g_object_set(self->fEncodeAppSrc, "caps", inCaps, NULL);
			gst_caps_unref(inCaps);
			self->fFrameStride = (raw.format
				& media_raw_audio_format::B_AUDIO_SIZE_MASK) * raw.channel_count;
		}
	}

	if (self->fEncodePipeline != NULL && self->fEncodeAppSrc != NULL
			&& self->fFrameStride > 0) {
		// The encoder elements appear once encodebin negotiates; retry
		// until the quality knobs can be set.
		if (self->fEncodeQuality >= 0.0f && !self->fQualityApplied
				&& self->fQualityTries++ < 200) {
			self->fQualityApplied
				= BPrivate::media::GStreamerBackend::ApplyEncoderQuality(
					self->fEncodePipeline, self->fEncodeQuality) > 0;
		}
		// gst_buffer_new_wrapped takes ownership — copy first.
		GstBuffer* gb = gst_buffer_new_allocate(NULL, size, NULL);
		if (gb == NULL)
			return;
		GstMapInfo map;
		if (gst_buffer_map(gb, &map, GST_MAP_WRITE)) {
			memcpy(map.data, buffer, size);
			gst_buffer_unmap(gb, &map);
			const float rate = format.format.u.raw_audio.frame_rate;
			if (rate > 0.0f) {
				const uint64 durNs = (uint64)((frameCount * GST_SECOND)
					/ (int64)rate);
				GST_BUFFER_PTS(gb)      = self->fEncodeNextPts;
				GST_BUFFER_DURATION(gb) = durNs;
				self->fEncodeNextPts += durNs;
			}
			GstFlowReturn r = gst_app_src_push_buffer(
				GST_APP_SRC(self->fEncodeAppSrc), gb);
				// push_buffer takes ownership on success
			if (r != GST_FLOW_OK && !self->fPushWarned) {
				self->fPushWarned = true;
				fprintf(stderr, "MediaRecorder: push_buffer: %s\n",
					gst_flow_get_name(r));
			}
		} else {
			gst_buffer_unref(gb);
		}
	}
}


// #pragma mark - public surface


BMediaRecorder::BMediaRecorder(const char* name, media_type type)
	:
	fImpl(new(std::nothrow) Impl(this, name, type))
{
}


// Legacy override hook — default no-op.
void
BMediaRecorder::BufferReceived(void*, size_t, const media_raw_audio_format&)
{
}


BMediaInput*
BMediaRecorder::Input() const
{
	return fImpl != NULL ? fImpl->Input() : NULL;
}


BMediaRecorder::~BMediaRecorder()
{
	delete fImpl;
}


status_t
BMediaRecorder::InitCheck() const
{
	return fImpl != NULL ? fImpl->InitCheck() : B_NO_MEMORY;
}


const BMediaFormat&
BMediaRecorder::Format() const
{
	static BMediaFormat sEmpty;
	return fImpl != NULL ? fImpl->Format() : sEmpty;
}


status_t  BMediaRecorder::SetFormat(const BMediaFormat& f)  { return fImpl ? fImpl->SetFormat(f) : B_NO_INIT; }
status_t  BMediaRecorder::SetAudioFormat(const BMediaFormat& f) { return fImpl ? fImpl->SetAudioFormat(f) : B_NO_INIT; }
status_t  BMediaRecorder::SetAudioSource(media_client_id d) { return fImpl ? fImpl->SetAudioSource(d) : B_NO_INIT; }
bool      BMediaRecorder::HasAudioSource() const { return fImpl && fImpl->HasAudioSource(); }
status_t  BMediaRecorder::Start(bool /*force*/)             { return fImpl ? fImpl->Start() : B_NO_INIT; }
status_t  BMediaRecorder::Stop(bool /*force*/)              { if (fImpl) fImpl->Stop(); return B_OK; }
bool      BMediaRecorder::IsRunning() const                 { return fImpl && fImpl->IsRecording(); }
size_t    BMediaRecorder::BufferSize() const                { return fImpl ? fImpl->BufferSize() : 0; }


BMediaInput*
BMediaRecorder::VideoInput() const
{
	return fImpl != NULL ? fImpl->VideoInput() : NULL;
}


BMediaInput*
BMediaRecorder::AudioInput() const
{
	return fImpl != NULL ? fImpl->AudioInput() : NULL;
}


BMediaClient*
BMediaRecorder::Client() const
{
	return fImpl != NULL ? fImpl->Client() : NULL;
}


status_t
BMediaRecorder::SetOutputFile(const char* path,
	const BMediaFormat& videoFormat, const BMediaFormat& audioFormat)
{
	return fImpl
		? fImpl->SetOutputFile(path, videoFormat, audioFormat) : B_NO_INIT;
}


void
BMediaRecorder::SetCallbacks(RecordBuffer recordFunc, Notifier notifyFunc,
	void* cookie)
{
	if (fImpl != NULL)
		fImpl->SetCallbacks(recordFunc, notifyFunc, cookie);
}


status_t
BMediaRecorder::SetBufferHooks(BufferFunc bufferFunc, NotifyFunc notifyFunc,
	void* cookie)
{
	if (fImpl == NULL)
		return B_NO_INIT;
	fImpl->SetBufferHooks(bufferFunc, notifyFunc, cookie);
	return B_OK;
}


status_t
BMediaRecorder::SetHooks(ProcessFunc recordFunc, NotifyFunc notifyFunc,
	void* cookie)
{
	if (fImpl == NULL)
		return B_NO_INIT;
	fImpl->SetCallbacks(recordFunc, notifyFunc, cookie);
	return B_OK;
}


status_t
BMediaRecorder::SetOutputFile(const char* path, const BMediaFormat& fmt,
	uint32 sampleRate, uint32 channelCount, sample_format sampleFormat,
	float quality)
{
	return fImpl ? fImpl->SetOutputFile(path, fmt, sampleRate, channelCount,
		sampleFormat, quality) : B_NO_INIT;
}


status_t
BMediaRecorder::SetOutputFile(BFile* file, const BMediaFormat& fmt,
	uint32 sampleRate, uint32 channelCount, sample_format sampleFormat,
	float quality)
{
	if (file == NULL)
		return B_BAD_VALUE;
	if (file->InitCheck() != B_OK)
		return file->InitCheck();

	node_ref nref;
	status_t status = file->GetNodeRef(&nref);
	if (status != B_OK)
		return status;
	char resolved[B_PATH_NAME_LENGTH];
	status = _kern_entry_ref_to_path(nref.vdevice(), nref.vnode(), NULL,
		resolved, sizeof(resolved));
	if (status != B_OK)
		return status;

	return SetOutputFile(resolved, fmt, sampleRate, channelCount,
		sampleFormat, quality);
}


bool
BMediaRecorder::IsEncodingAvailable(int32 encoding)
{
	return BPrivate::media::GStreamerBackend::IsEncoderAvailable(encoding);
}


void
BMediaRecorder::ClearOutputFile()
{
	if (fImpl != NULL)
		fImpl->ClearOutputFile();
}
