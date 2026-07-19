/*
 * Copyright 2025-2026, Dario Casalinuovo. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/MediaFile.h>
#include <media2/MediaTrack.h>

#include <new>
#include <stdio.h>
#include <string.h>
#include <string>

#include <Entry.h>
#include <Path.h>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>

#include "GStreamerBackend.h"


using namespace BPrivate::media;


// #pragma mark - BMediaTrack::Impl


class BMediaTrack::Impl {
public:
	Impl()
		:
		fAppSink(NULL),
		fAppSrc(NULL),
		fPipeline(NULL),
		fInitErr(B_NO_INIT),
		fWriteMode(false),
		fEOS(false),
		fFrameStride(0),
		fNextPts(0)
	{
	}

	GstElement*		fAppSink;	// read mode  (borrowed; owned by pipeline)
	GstElement*		fAppSrc;	// write mode (borrowed)
	GstElement*		fPipeline;	// borrowed
	BMediaFormat	fFormat;
	status_t		fInitErr;
	bool			fWriteMode;
	bool			fEOS;
	uint32			fFrameStride;
	uint64			fNextPts;	// nanoseconds since stream start
};


// #pragma mark - BMediaFile::Impl


class BMediaFile::Impl {
public:
	Impl()
		:
		fPipeline(NULL),
		fAppSink(NULL),
		fAppSrc(NULL),
		fTrack(NULL),
		fInitErr(B_NO_INIT),
		fWriteMode(false),
		fCodecType(0)
	{
	}

	~Impl()
	{
		if (fTrack != NULL) {
			// fTrack->fImpl borrows pipeline pointers, so destroy track first.
			delete fTrack;
			fTrack = NULL;
		}
		if (fPipeline != NULL) {
			gst_element_set_state(fPipeline, GST_STATE_NULL);
			gst_object_unref(fPipeline);
			fPipeline = NULL;
		}
	}

	status_t Open(const entry_ref* ref)
	{
		if (ref == NULL)
			return B_BAD_VALUE;

		BPath path(ref);
		if (path.InitCheck() != B_OK)
			return path.InitCheck();

		// Ensure GStreamer is initialised.
		if (GStreamerBackend::GetInstance() == NULL)
			return B_NO_INIT;

		gchar* uri = gst_filename_to_uri(path.Path(), NULL);
		if (uri == NULL)
			return B_BAD_VALUE;

		fPipeline = gst_element_factory_make("playbin", "vitruvian-playbin");
		if (fPipeline == NULL) {
			g_free(uri);
			return B_ERROR;
		}

		// Restrict the audio sink to F32LE interleaved so the rest of the
		// kit deals with a known format. PipeWire and BSoundPlayer can both
		// consume this without further conversion.
		GstCaps* caps = gst_caps_new_simple("audio/x-raw",
			"format",   G_TYPE_STRING, "F32LE",
			"layout",   G_TYPE_STRING, "interleaved",
			NULL);

		fAppSink = gst_element_factory_make("appsink", "vitruvian-appsink");
		if (fAppSink == NULL) {
			gst_caps_unref(caps);
			g_free(uri);
			gst_object_unref(fPipeline);
			fPipeline = NULL;
			return B_ERROR;
		}
		g_object_set(fAppSink,
			"caps",          caps,
			"sync",          FALSE,
			"max-buffers",   8,
			"drop",          FALSE,
			NULL);
		gst_caps_unref(caps);

		GstElement* fakeVideo = gst_element_factory_make("fakesink", "video-sink");

		g_object_set(fPipeline,
			"uri",         uri,
			"audio-sink",  fAppSink,
			"video-sink",  fakeVideo,
			NULL);
		g_free(uri);

		// PAUSED runs preroll — by the time it completes, caps are negotiated.
		GstStateChangeReturn r = gst_element_set_state(fPipeline,
			GST_STATE_PAUSED);
		if (r == GST_STATE_CHANGE_FAILURE)
			return B_ERROR;
		r = gst_element_get_state(fPipeline, NULL, NULL,
			5 * GST_SECOND);
		if (r == GST_STATE_CHANGE_FAILURE)
			return B_ERROR;

		// Read the negotiated caps from the appsink sink-pad.
		GstPad* pad = gst_element_get_static_pad(fAppSink, "sink");
		if (pad == NULL)
			return B_ERROR;
		GstCaps* negotiated = gst_pad_get_current_caps(pad);
		gst_object_unref(pad);
		if (negotiated == NULL)
			return B_ERROR;

		BMediaFormat fmt;
		bool ok = GStreamerBackend::GstCapsToRawAudio(negotiated, &fmt);
		gst_caps_unref(negotiated);
		if (!ok)
			return B_ERROR;

		// Build the single BMediaTrack.
		fTrack = new(std::nothrow) BMediaTrack();
		if (fTrack == NULL)
			return B_NO_MEMORY;
		fTrack->fImpl->fAppSink  = fAppSink;
		fTrack->fImpl->fPipeline = fPipeline;
		fTrack->fImpl->fFormat   = fmt;
		fTrack->fImpl->fInitErr  = B_OK;

		// Transition to PLAYING so the pipeline pulls data on demand.
		gst_element_set_state(fPipeline, GST_STATE_PLAYING);

		fInitErr = B_OK;
		return B_OK;
	}

	status_t OpenWrite(const entry_ref* ref, int32 codecType)
	{
		if (ref == NULL)
			return B_BAD_VALUE;
		BPath path(ref);
		if (path.InitCheck() != B_OK)
			return path.InitCheck();
		if (GStreamerBackend::GetInstance() == NULL)
			return B_NO_INIT;
		fOutputPath = path.Path();
		fCodecType  = codecType;
		fWriteMode  = true;
		fInitErr    = B_OK;
		return B_OK;
	}

	BMediaTrack* CreateTrack(const BMediaFormat& outputFormat)
	{
		if (!fWriteMode || fTrack != NULL)
			return NULL;

		BMediaFormat encFmt = outputFormat;
		if (!encFmt.IsEncodedAudio()) {
			// Caller supplied a raw format — wrap it as encoded with our codec.
			BMediaFormat wrap;
			wrap.format.type = B_MEDIA_ENCODED_AUDIO;
			media_encoded_audio_format& enc = wrap.format.u.encoded_audio;
			memset(&enc, 0, sizeof(enc));
			enc.encoding = fCodecType;
			if (outputFormat.IsRawAudio())
				enc.output = outputFormat.format.u.raw_audio;
			encFmt = wrap;
		} else {
			encFmt.format.u.encoded_audio.encoding = fCodecType;
		}

		fPipeline = GStreamerBackend::GetInstance()->CreateEncodePipeline(
			fOutputPath.c_str(), encFmt);
		if (fPipeline == NULL)
			return NULL;

		fAppSrc = gst_bin_get_by_name(GST_BIN(fPipeline), "src");
		if (fAppSrc == NULL) {
			gst_element_set_state(fPipeline, GST_STATE_NULL);
			gst_object_unref(fPipeline);
			fPipeline = NULL;
			return NULL;
		}
		// gst_bin_get_by_name returns a new ref; we want a borrowed ref to
		// match the read-mode pattern. Drop the extra ref but keep the ptr —
		// the bin still owns it.
		gst_object_unref(fAppSrc);

		// Configure appsrc raw input caps so encodebin knows what's coming in.
		BMediaFormat rawIn;
		if (outputFormat.IsRawAudio()) {
			rawIn = outputFormat;
		} else {
			rawIn.format.type = B_MEDIA_RAW_AUDIO;
			rawIn.format.u.raw_audio = encFmt.format.u.encoded_audio.output;
			// Defaults to interleaved + default positions; matches our
			// CommitHeader expectations.
			rawIn.is_planar = false;
		}
		GstCaps* inCaps = GStreamerBackend::RawAudioToGstCaps(rawIn);
		if (inCaps != NULL) {
			g_object_set(fAppSrc, "caps", inCaps,
				"format", GST_FORMAT_TIME,
				"is-live", FALSE,
				NULL);
			gst_caps_unref(inCaps);
		}

		fTrack = new(std::nothrow) BMediaTrack();
		if (fTrack == NULL)
			return NULL;
		fTrack->fImpl->fAppSrc    = fAppSrc;
		fTrack->fImpl->fPipeline  = fPipeline;
		fTrack->fImpl->fFormat    = rawIn;
		fTrack->fImpl->fWriteMode = true;
		const media_raw_audio_format& raw = rawIn.format.u.raw_audio;
		fTrack->fImpl->fFrameStride = (raw.format
			& media_raw_audio_format::B_AUDIO_SIZE_MASK) * raw.channel_count;
		fTrack->fImpl->fInitErr   = B_OK;
		return fTrack;
	}

	status_t CommitHeader()
	{
		if (!fWriteMode || fPipeline == NULL)
			return B_NO_INIT;
		GstStateChangeReturn r = gst_element_set_state(fPipeline,
			GST_STATE_PLAYING);
		if (r == GST_STATE_CHANGE_FAILURE)
			return B_ERROR;
		return B_OK;
	}

	status_t CloseFile()
	{
		if (!fWriteMode || fPipeline == NULL)
			return B_OK;
		if (fAppSrc != NULL) {
			gst_app_src_end_of_stream(GST_APP_SRC(fAppSrc));
		}
		// Wait for EOS to propagate to filesink, then bring pipeline down.
		GstBus* bus = gst_element_get_bus(fPipeline);
		if (bus != NULL) {
			gst_bus_timed_pop_filtered(bus, 5 * GST_SECOND,
				(GstMessageType)(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
			gst_object_unref(bus);
		}
		gst_element_set_state(fPipeline, GST_STATE_NULL);
		return B_OK;
	}

	GstElement*		fPipeline;
	GstElement*		fAppSink;
	GstElement*		fAppSrc;
	BMediaTrack*	fTrack;
	status_t		fInitErr;
	bool			fWriteMode;
	int32			fCodecType;
	std::string		fOutputPath;
};


// #pragma mark - BMediaFile public surface


BMediaFile::BMediaFile(const entry_ref* ref)
	:
	fImpl(new(std::nothrow) Impl())
{
	if (fImpl == NULL)
		return;
	fImpl->fInitErr = fImpl->Open(ref);
}


BMediaFile::BMediaFile(const entry_ref* ref, int32 /*flags*/)
	:
	fImpl(new(std::nothrow) Impl())
{
	if (fImpl == NULL)
		return;
	fImpl->fInitErr = fImpl->Open(ref);
}


static int32
CodecTypeFromFileFormat(const media_file_format* f)
{
	if (f == NULL)
		return B_CODEC_TYPE_UNKNOWN;
	// Match on mime_type first, then short_name; same table as
	// MediaFormats.cpp's MimeToCodecType but the file_format's mime
	// is the container mime, so it lines up.
	const char* mime = f->mime_type;
	const char* sn   = f->short_name;
	if ((mime && strstr(mime, "audio/x-wav"))
		|| (sn && (strstr(sn, "wav") || strstr(sn, "WAV"))))
		return B_CODEC_TYPE_WAV;
	if (mime && strstr(mime, "application/ogg"))
		return B_CODEC_TYPE_OGG;
	if (mime && strstr(mime, "audio/x-flac"))
		return B_CODEC_TYPE_FLAC;
	if (mime && strstr(mime, "audio/mpeg"))
		return B_CODEC_TYPE_MP3;
	if (mime && (strstr(mime, "video/quicktime")
			|| strstr(mime, "video/mp4")))
		return B_CODEC_TYPE_AAC;
	if (mime && strstr(mime, "audio/x-opus"))
		return B_CODEC_TYPE_OPUS;
	return B_CODEC_TYPE_WAV;	// safe default
}


BMediaFile::BMediaFile(const entry_ref* outputRef,
	const media_file_format* fileFormat, int32 /*flags*/)
	:
	fImpl(new(std::nothrow) Impl())
{
	if (fImpl == NULL)
		return;
	fImpl->fInitErr = fImpl->OpenWrite(outputRef,
		CodecTypeFromFileFormat(fileFormat));
}


BMediaTrack*
BMediaFile::CreateTrack(const BMediaFormat& outputFormat)
{
	return fImpl != NULL ? fImpl->CreateTrack(outputFormat) : NULL;
}


BMediaTrack*
BMediaFile::CreateTrack(media_format* mf, const media_codec_info* codecInfo)
{
	if (mf == NULL)
		return NULL;
	// Wrap the legacy media_format in a BMediaFormat; if a codec is supplied,
	// promote audio/video raw to encoded and set encoding to codecInfo->id.
	BMediaFormat fmt;
	fmt.format = *mf;
	if (codecInfo != NULL) {
		if (fmt.format.type == B_MEDIA_RAW_AUDIO) {
			media_encoded_audio_format ea = {};
			ea.output   = fmt.format.u.raw_audio;
			ea.encoding = codecInfo->id;
			fmt.format.type = B_MEDIA_ENCODED_AUDIO;
			fmt.format.u.encoded_audio = ea;
		} else if (fmt.format.type == B_MEDIA_RAW_VIDEO) {
			media_encoded_video_format ev = {};
			ev.output   = fmt.format.u.raw_video;
			ev.encoding = codecInfo->id;
			fmt.format.type = B_MEDIA_ENCODED_VIDEO;
			fmt.format.u.encoded_video = ev;
		} else if (fmt.format.type == B_MEDIA_ENCODED_AUDIO) {
			fmt.format.u.encoded_audio.encoding = codecInfo->id;
		} else if (fmt.format.type == B_MEDIA_ENCODED_VIDEO) {
			fmt.format.u.encoded_video.encoding = codecInfo->id;
		}
	}
	return CreateTrack(fmt);
}


status_t
BMediaFile::CommitHeader()
{
	return fImpl != NULL ? fImpl->CommitHeader() : B_NO_INIT;
}


status_t
BMediaFile::CloseFile()
{
	return fImpl != NULL ? fImpl->CloseFile() : B_OK;
}


BMediaFile::~BMediaFile()
{
	delete fImpl;
}


status_t
BMediaFile::InitCheck() const
{
	return fImpl != NULL ? fImpl->fInitErr : B_NO_MEMORY;
}


int32
BMediaFile::CountTracks() const
{
	if (fImpl == NULL || fImpl->fInitErr != B_OK)
		return 0;
	return fImpl->fTrack != NULL ? 1 : 0;
}


BMediaTrack*
BMediaFile::TrackAt(int32 index)
{
	if (index != 0 || fImpl == NULL || fImpl->fInitErr != B_OK)
		return NULL;
	return fImpl->fTrack;
}


status_t
BMediaFile::ReleaseTrack(BMediaTrack* /*track*/)
{
	// Tracks are owned by BMediaFile in this implementation.
	return B_OK;
}


// #pragma mark - BMediaTrack public surface


BMediaTrack::BMediaTrack()
	:
	fImpl(new(std::nothrow) Impl())
{
}


BMediaTrack::~BMediaTrack()
{
	delete fImpl;
}


status_t
BMediaTrack::InitCheck() const
{
	return fImpl != NULL ? fImpl->fInitErr : B_NO_MEMORY;
}


status_t
BMediaTrack::DecodedFormat(BMediaFormat* format)
{
	if (format == NULL || fImpl == NULL || fImpl->fInitErr != B_OK)
		return B_BAD_VALUE;
	*format = fImpl->fFormat;
	return B_OK;
}


status_t
BMediaTrack::EncodedFormat(BMediaFormat* format)
{
	// playbin's appsink always sits after decoding, so we don't have a
	// handle on the source-level codec caps. Report B_MEDIA_ENCODED_AUDIO
	// wrapping the decoded raw_audio so callers probing for "is this an
	// audio track" + frame-rate/channel-count get a useful answer.
	if (format == NULL || fImpl == NULL || fImpl->fInitErr != B_OK)
		return B_BAD_VALUE;
	if (!fImpl->fFormat.IsRawAudio())
		return B_ERROR;
	*format = BMediaFormat();
	format->format.type = B_MEDIA_ENCODED_AUDIO;
	format->format.u.encoded_audio.output = fImpl->fFormat.format.u.raw_audio;
	format->format.u.encoded_audio.encoding = B_CODEC_TYPE_UNKNOWN;
	return B_OK;
}


int64
BMediaTrack::CountFrames() const
{
	if (fImpl == NULL || fImpl->fPipeline == NULL)
		return 0;
	gint64 dur = 0;
	if (!gst_element_query_duration(fImpl->fPipeline, GST_FORMAT_TIME, &dur))
		return 0;
	const media_raw_audio_format& raw = fImpl->fFormat.format.u.raw_audio;
	return (int64)((dur * (gint64)raw.frame_rate) / GST_SECOND);
}


bigtime_t
BMediaTrack::Duration() const
{
	if (fImpl == NULL || fImpl->fPipeline == NULL)
		return 0;
	gint64 dur = 0;
	if (!gst_element_query_duration(fImpl->fPipeline, GST_FORMAT_TIME, &dur))
		return 0;
	return (bigtime_t)(dur / 1000);	// ns → µs
}


bigtime_t
BMediaTrack::CurrentTime() const
{
	if (fImpl == NULL || fImpl->fPipeline == NULL)
		return 0;
	gint64 pos = 0;
	if (!gst_element_query_position(fImpl->fPipeline, GST_FORMAT_TIME, &pos))
		return 0;
	return (bigtime_t)(pos / 1000);	// ns → µs
}


int64
BMediaTrack::CurrentFrame() const
{
	if (fImpl == NULL)
		return 0;
	const media_raw_audio_format& raw = fImpl->fFormat.format.u.raw_audio;
	if (raw.frame_rate <= 0.0f)
		return 0;
	return (int64)((CurrentTime() * (int64)raw.frame_rate) / 1000000LL);
}


status_t
BMediaTrack::ReadFrames(void* buffer, int64* outFrameCount, media_header* mh)
{
	if (buffer == NULL || outFrameCount == NULL
			|| fImpl == NULL || fImpl->fAppSink == NULL) {
		return B_BAD_VALUE;
	}
	const int64 requested = *outFrameCount;
	*outFrameCount = 0;
	if (requested <= 0)
		return B_BAD_VALUE;

	if (fImpl->fEOS)
		return B_LAST_BUFFER_ERROR;

	GstAppSink* sink = GST_APP_SINK(fImpl->fAppSink);
	GstSample* sample = gst_app_sink_pull_sample(sink);
	if (sample == NULL) {
		if (gst_app_sink_is_eos(sink)) {
			fImpl->fEOS = true;
			return B_LAST_BUFFER_ERROR;
		}
		return B_ERROR;
	}

	GstBuffer* gb = gst_sample_get_buffer(sample);
	GstMapInfo map;
	if (gb == NULL || !gst_buffer_map(gb, &map, GST_MAP_READ)) {
		gst_sample_unref(sample);
		return B_ERROR;
	}

	const media_raw_audio_format& raw = fImpl->fFormat.format.u.raw_audio;
	const uint32 stride = (raw.format & media_raw_audio_format::B_AUDIO_SIZE_MASK)
		* raw.channel_count;
	int64 framesAvail = stride > 0 ? (int64)(map.size / stride) : 0;
	int64 framesToCopy = framesAvail < requested ? framesAvail : requested;
	const size_t bytes = framesToCopy * stride;

	memcpy(buffer, map.data, bytes);
	*outFrameCount = framesToCopy;

	if (mh != NULL) {
		memset(mh, 0, sizeof(*mh));
		if (GST_BUFFER_PTS_IS_VALID(gb))
			mh->start_time = (bigtime_t)(GST_BUFFER_PTS(gb) / 1000);
		mh->size_used = (uint32)bytes;
	}

	gst_buffer_unmap(gb, &map);
	gst_sample_unref(sample);
	return B_OK;
}


status_t
BMediaTrack::SeekToTime(bigtime_t* inOutTime, int32 /*flags*/)
{
	if (inOutTime == NULL || fImpl == NULL || fImpl->fPipeline == NULL)
		return B_BAD_VALUE;
	const gint64 ns = (gint64)*inOutTime * 1000;
	if (!gst_element_seek_simple(fImpl->fPipeline, GST_FORMAT_TIME,
			(GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT), ns)) {
		return B_ERROR;
	}
	fImpl->fEOS = false;
	return B_OK;
}


status_t
BMediaTrack::SeekToFrame(int64* inOutFrame, int32 flags)
{
	if (inOutFrame == NULL || fImpl == NULL)
		return B_BAD_VALUE;
	const media_raw_audio_format& raw = fImpl->fFormat.format.u.raw_audio;
	if (raw.frame_rate <= 0.0f)
		return B_BAD_VALUE;
	bigtime_t t = (bigtime_t)((*inOutFrame * 1000000LL) / (int64)raw.frame_rate);
	return SeekToTime(&t, flags);
}


status_t
BMediaTrack::WriteFrames(const void* buffer, int64 frameCount,
	const media_header* /*mh*/)
{
	if (buffer == NULL || frameCount <= 0
			|| fImpl == NULL || !fImpl->fWriteMode || fImpl->fAppSrc == NULL) {
		return B_BAD_VALUE;
	}

	const media_raw_audio_format& raw = fImpl->fFormat.format.u.raw_audio;
	const size_t bytes = (size_t)frameCount * fImpl->fFrameStride;

	GstBuffer* gb = gst_buffer_new_allocate(NULL, bytes, NULL);
	if (gb == NULL)
		return B_NO_MEMORY;
	GstMapInfo map;
	if (!gst_buffer_map(gb, &map, GST_MAP_WRITE)) {
		gst_buffer_unref(gb);
		return B_ERROR;
	}
	memcpy(map.data, buffer, bytes);
	gst_buffer_unmap(gb, &map);

	if (raw.frame_rate > 0.0f) {
		const uint64 durationNs = (uint64)((frameCount * GST_SECOND)
			/ (int64)raw.frame_rate);
		GST_BUFFER_PTS(gb)      = fImpl->fNextPts;
		GST_BUFFER_DURATION(gb) = durationNs;
		fImpl->fNextPts += durationNs;
	}

	GstFlowReturn r = gst_app_src_push_buffer(GST_APP_SRC(fImpl->fAppSrc), gb);
		// push_buffer takes ownership of gb on success
	if (r != GST_FLOW_OK)
		return B_ERROR;
	return B_OK;
}


status_t
BMediaTrack::GetCodecInfo(media_codec_info* info) const
{
	if (info == NULL || fImpl == NULL)
		return B_BAD_VALUE;
	memset(info, 0, sizeof(*info));
	int32 encoding = B_CODEC_TYPE_UNKNOWN;
	if (fImpl->fFormat.format.type == B_MEDIA_ENCODED_AUDIO)
		encoding = fImpl->fFormat.format.u.encoded_audio.encoding;
	else if (fImpl->fFormat.format.type == B_MEDIA_ENCODED_VIDEO)
		encoding = fImpl->fFormat.format.u.encoded_video.encoding;
	info->id = encoding;
	snprintf(info->short_name, sizeof(info->short_name), "codec-%d", (int)encoding);
	snprintf(info->pretty_name, sizeof(info->pretty_name), "Codec %d", (int)encoding);
	return B_OK;
}


status_t
BMediaTrack::SetQuality(float /*quality*/)
{
	// Encoding profile already carries quality via the GstEncodingProfile API.
	// Per-track runtime quality changes aren't surfaced by GStreamer's encodebin
	// for typical audio encoders; returning B_OK keeps legacy callers happy.
	return B_OK;
}


status_t
BMediaTrack::Flush()
{
	if (fImpl == NULL || fImpl->fPipeline == NULL)
		return B_NO_INIT;
	return B_OK;
}


BView*
BMediaTrack::GetParameterView()
{
	// Codec-parameter UI not surfaced from GStreamer (the underlying encoder's
	// properties vary too widely to expose generically).
	return NULL;
}
