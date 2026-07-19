/*
 * Copyright 2025-2026, Dario Casalinuovo. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/MediaRecorder.h>

#include <new>
#include <string.h>

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

#include <Entry.h>
#include <File.h>
#include <Path.h>
#include <syscalls.h>

#include <media2/MediaConnection.h>
#include <media2/SimpleMediaNode.h>

#include "GStreamerBackend.h"


namespace {


class _InputConnection : public BMediaInput {
public:
	_InputConnection()
		:
		BMediaConnection(B_MEDIA_INPUT, "BMediaRecorder-in"),
		BMediaInput("BMediaRecorder-in")
	{
	}
	virtual ~_InputConnection() {}

protected:
	virtual status_t AcceptFormat(BMediaFormat* /*format*/) override { return B_OK; }
};


} // anonymous namespace


class BMediaRecorder::Impl {
public:
	Impl(BMediaRecorder* owner, const char* name, media_type type);
	~Impl();

	status_t InitCheck() const { return fInitErr; }

	const BMediaFormat& Format() const { return fFormat; }
	status_t SetFormat(const BMediaFormat& format);

	status_t Start();
	void     Stop();
	bool     IsRecording() const { return fNode != NULL && fNode->IsStarted(); }

	void SetCallbacks(BMediaRecorder::RecordBuffer recordFunc,
		BMediaRecorder::Notifier notifyFunc, void* cookie);

	size_t BufferSize() const { return fFormat.format.u.raw_audio.buffer_size; }

	status_t SetOutputFile(const char* path,
		const BMediaFormat& encodedFormat);
	void     ClearOutputFile();

private:
	static void _ProcessHook(void* cookie, void* buffer, size_t size,
		uint32 frameCount, const BMediaFormat& format);

	BMediaRecorder*	fOwner;
	BMediaFormat	fFormat;
	BSimpleMediaNode*		fNode;
	_InputConnection*		fInput;
	status_t				fInitErr;

	BMediaRecorder::RecordBuffer	fRecordFunc;
	BMediaRecorder::Notifier		fNotifyFunc;
	void*							fCookie;

	// Encode-to-file
	GstElement*			fEncodePipeline;
	GstElement*			fEncodeAppSrc;	// borrowed (owned by pipeline)
	uint32				fFrameStride;
	uint64				fEncodeNextPts;	// ns
};


BMediaRecorder::Impl::Impl(BMediaRecorder* owner, const char* name,
	media_type type)
	:
	fOwner(owner),
	fFormat(),
	fNode(NULL),
	fInput(NULL),
	fInitErr(B_NO_INIT),
	fRecordFunc(NULL),
	fNotifyFunc(NULL),
	fCookie(NULL),
	fEncodePipeline(NULL),
	fEncodeAppSrc(NULL),
	fFrameStride(0),
	fEncodeNextPts(0)
{
	fFormat.SetToDefault();

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

	fInput = new(std::nothrow) _InputConnection();
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
	ClearOutputFile();
	delete fInput;
	delete fNode;
}


status_t
BMediaRecorder::Impl::SetFormat(const BMediaFormat& fmt)
{
	if (fNode == NULL)
		return B_NO_INIT;
	if (fNode->IsStarted())
		return B_NOT_ALLOWED;
	fFormat = fmt;
	return fNode->SetFormat(fmt);
}


status_t
BMediaRecorder::Impl::Start()
{
	if (fNode == NULL)
		return B_NO_INIT;
	status_t err = fNode->Start();
	if (err != B_OK)
		return err;
	if (fNotifyFunc != NULL)
		fNotifyFunc(fCookie, BMediaRecorder::B_STARTED);
	return B_OK;
}


void
BMediaRecorder::Impl::Stop()
{
	if (fNode == NULL || !fNode->IsStarted())
		return;
	fNode->Stop();
	if (fEncodePipeline != NULL) {
		gst_app_src_end_of_stream(GST_APP_SRC(fEncodeAppSrc));
		GstBus* bus = gst_element_get_bus(fEncodePipeline);
		if (bus != NULL) {
			gst_bus_timed_pop_filtered(bus, 5 * GST_SECOND,
				(GstMessageType)(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
			gst_object_unref(bus);
		}
		gst_element_set_state(fEncodePipeline, GST_STATE_NULL);
	}
	if (fNotifyFunc != NULL)
		fNotifyFunc(fCookie, BMediaRecorder::B_STOPPED);
}


status_t
BMediaRecorder::Impl::SetOutputFile(const char* path,
	const BMediaFormat& encodedFormat)
{
	if (path == NULL)
		return B_BAD_VALUE;
	if (fNode != NULL && fNode->IsStarted())
		return B_NOT_ALLOWED;

	using namespace BPrivate::media;
	GStreamerBackend* gst = GStreamerBackend::GetInstance();
	if (gst == NULL)
		return B_NO_INIT;

	ClearOutputFile();

	fEncodePipeline = gst->CreateEncodePipeline(path, encodedFormat);
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

	// Configure appsrc with our capture format (raw side of the encoder).
	GstCaps* inCaps = GStreamerBackend::RawAudioToGstCaps(fFormat);
	if (inCaps != NULL) {
		g_object_set(fEncodeAppSrc,
			"caps",     inCaps,
			"format",   GST_FORMAT_TIME,
			"is-live",  TRUE,
			NULL);
		gst_caps_unref(inCaps);
	}

	const media_raw_audio_format& raw = fFormat.format.u.raw_audio;
	fFrameStride = (raw.format & media_raw_audio_format::B_AUDIO_SIZE_MASK)
		* raw.channel_count;
	fEncodeNextPts = 0;

	if (gst_element_set_state(fEncodePipeline, GST_STATE_PLAYING)
			== GST_STATE_CHANGE_FAILURE) {
		ClearOutputFile();
		return B_ERROR;
	}
	return B_OK;
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
BMediaRecorder::Impl::_ProcessHook(void* cookie, void* buffer, size_t size,
	uint32 frameCount, const BMediaFormat& format)
{
	Impl* self = (Impl*)cookie;

	if (self->fRecordFunc != NULL) {
		// Legacy ProcessFunc signature: (cookie, timestamp, data, size, format)
		self->fRecordFunc(self->fCookie, /*timestamp*/ 0, buffer, size,
			format.format.u.raw_audio);
	} else if (self->fOwner != NULL) {
		self->fOwner->BufferReceived(buffer, size, format.format.u.raw_audio);
	}

	if (self->fEncodePipeline != NULL && self->fEncodeAppSrc != NULL
			&& self->fFrameStride > 0) {
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
			gst_app_src_push_buffer(GST_APP_SRC(self->fEncodeAppSrc), gb);
				// push_buffer takes ownership on success
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
status_t  BMediaRecorder::Start(bool /*force*/)             { return fImpl ? fImpl->Start() : B_NO_INIT; }
status_t  BMediaRecorder::Stop(bool /*force*/)              { if (fImpl) fImpl->Stop(); return B_OK; }
bool      BMediaRecorder::IsRunning() const                 { return fImpl && fImpl->IsRecording(); }
size_t    BMediaRecorder::BufferSize() const                { return fImpl ? fImpl->BufferSize() : 0; }


void
BMediaRecorder::SetCallbacks(RecordBuffer recordFunc, Notifier notifyFunc,
	void* cookie)
{
	if (fImpl != NULL)
		fImpl->SetCallbacks(recordFunc, notifyFunc, cookie);
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
BMediaRecorder::SetOutputFile(const char* path, const BMediaFormat& fmt)
{
	return fImpl ? fImpl->SetOutputFile(path, fmt) : B_NO_INIT;
}


status_t
BMediaRecorder::SetOutputFile(BFile* file, const BMediaFormat& fmt)
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
	status = _kern_entry_ref_to_path(nref.device, nref.node, NULL,
		resolved, sizeof(resolved));
	if (status != B_OK)
		return status;

	return SetOutputFile(resolved, fmt);
}


void
BMediaRecorder::ClearOutputFile()
{
	if (fImpl != NULL)
		fImpl->ClearOutputFile();
}
