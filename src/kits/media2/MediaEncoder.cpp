/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <media2/MediaEncoder.h>

#include <new>
#include <string.h>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>

#include "GStreamerBackend.h"
#include "MediaEncodeTables.h"


using namespace BPrivate::media;


class BMediaEncoder::Impl {
public:
	Impl(BMediaEncoder* owner)
		:
		fOwner(owner),
		fPipeline(NULL),
		fAppSrc(NULL),
		fAppSink(NULL),
		fInitErr(B_NO_INIT),
		fCodecId(B_CODEC_TYPE_UNKNOWN),
		fFrameStride(0),
		fNextPts(0)
	{
	}

	~Impl()
	{
		if (fPipeline != NULL) {
			gst_element_set_state(fPipeline, GST_STATE_NULL);
			gst_object_unref(fPipeline);
		}
	}

	status_t SetTo(const BMediaFormat& outputFormat)
	{
		if (GStreamerBackend::GetInstance() == NULL)
			return B_NO_INIT;

		int32 codecId = B_CODEC_TYPE_UNKNOWN;
		BMediaFormat rawIn;
		if (outputFormat.IsEncodedAudio()) {
			codecId = outputFormat.format.u.encoded_audio.encoding;
			rawIn.format.type = B_MEDIA_RAW_AUDIO;
			rawIn.format.u.raw_audio
				= outputFormat.format.u.encoded_audio.output;
			rawIn.is_planar = false;
		} else if (outputFormat.IsEncodedVideo()) {
			codecId = outputFormat.format.u.encoded_video.encoding;
			rawIn.format.type = B_MEDIA_RAW_VIDEO;
			rawIn.format.u.raw_video
				= outputFormat.format.u.encoded_video.output;
		} else {
			return B_BAD_VALUE;
		}

		const EncodeCodecInfo* codec = FindEncodeCodec(codecId);
		if (codec == NULL || !IsEncodeCodecAvailable(codecId))
			return B_BAD_VALUE;

		// Tear down a previous encoder, if any.
		if (fPipeline != NULL) {
			gst_element_set_state(fPipeline, GST_STATE_NULL);
			gst_object_unref(fPipeline);
			fPipeline = NULL;
			fAppSrc = NULL;
			fAppSink = NULL;
		}

		GstElement* pipeline = gst_pipeline_new("vitruvian-encoder");
		GstElement* appSrc = gst_element_factory_make("appsrc", "src");
		GstElement* appSink = gst_element_factory_make("appsink", "sink");
		const char* convertName = codec->audio ? "audioconvert"
			: "videoconvert";
		GstElement* convert = gst_element_factory_make(convertName,
			"convert");
		GstElementFactory* factory = FindEncodeFactory(codec);
		GstElement* encoder = NULL;
		if (factory != NULL) {
			encoder = gst_element_factory_create(factory, "encoder");
			gst_object_unref(factory);
		}
		if (pipeline == NULL || appSrc == NULL || appSink == NULL
				|| convert == NULL || encoder == NULL) {
			if (pipeline != NULL)
				gst_object_unref(pipeline);
			if (appSrc != NULL)
				gst_object_unref(appSrc);
			if (appSink != NULL)
				gst_object_unref(appSink);
			if (convert != NULL)
				gst_object_unref(convert);
			if (encoder != NULL)
				gst_object_unref(encoder);
			return B_ERROR;
		}

		g_object_set(appSink, "sync", FALSE, "emit-signals", FALSE, NULL);
		GstCaps* inCaps = codec->audio
			? GStreamerBackend::RawAudioToGstCaps(rawIn)
			: GStreamerBackend::RawVideoToGstCaps(rawIn);
		if (inCaps == NULL) {
			gst_object_unref(pipeline);
			return B_BAD_VALUE;
		}
		g_object_set(appSrc, "caps", inCaps, "format", GST_FORMAT_TIME,
			"is-live", FALSE, NULL);
		gst_caps_unref(inCaps);

		GstBin* bin = GST_BIN(pipeline);
		gst_bin_add_many(bin, appSrc, convert, encoder, appSink, NULL);
		if (!gst_element_link_many(appSrc, convert, encoder, appSink,
				NULL)) {
			gst_object_unref(pipeline);
			return B_ERROR;
		}

		GstStateChangeReturn r = gst_element_set_state(pipeline,
			GST_STATE_PLAYING);
		if (r == GST_STATE_CHANGE_FAILURE) {
			gst_object_unref(pipeline);
			return B_ERROR;
		}

		// Borrowed from the pipeline, matching the kit-wide pattern.
		fPipeline = pipeline;
		fAppSrc = appSrc;
		fAppSink = appSink;
		fCodecId = codecId;
		fRawFormat = rawIn;
		if (codec->audio) {
			const media_raw_audio_format& raw
				= rawIn.format.u.raw_audio;
			fFrameStride = (raw.format
				& media_raw_audio_format::B_AUDIO_SIZE_MASK)
				* raw.channel_count;
		} else {
			fFrameStride = rawIn.format.u.raw_video.display.bytes_per_row;
		}
		fNextPts = 0;
		fInitErr = B_OK;
		return B_OK;
	}

	status_t Encode(const void* buffer, int64 frameCount,
		media_encode_info* info)
	{
		if (fInitErr != B_OK || buffer == NULL || frameCount <= 0)
			return B_BAD_VALUE;
		if (fFrameStride == 0)
			return B_NO_INIT;

		const size_t bytes = (size_t)frameCount * fFrameStride;
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

		float rate = fRawFormat.IsRawAudio()
			? fRawFormat.format.u.raw_audio.frame_rate
			: fRawFormat.format.u.raw_video.field_rate;
		if (rate > 0.0f) {
			const uint64 durationNs = (uint64)((frameCount * GST_SECOND)
				/ (int64)rate);
			GST_BUFFER_PTS(gb)      = fNextPts;
			GST_BUFFER_DURATION(gb) = durationNs;
			fNextPts += durationNs;
		}

		GstFlowReturn r = gst_app_src_push_buffer(GST_APP_SRC(fAppSrc), gb);
			// push_buffer takes ownership of gb on success
		if (r != GST_FLOW_OK)
			return B_ERROR;

		return _Drain(info);
	}

	status_t _Drain(media_encode_info* info)
	{
		if (fAppSink == NULL)
			return B_NO_INIT;
		while (true) {
			// The encoder element runs on its own thread; give it a
			// short window to emit before deciding the drain is done.
			GstSample* sample = gst_app_sink_try_pull_sample(
				GST_APP_SINK(fAppSink), 100 * GST_MSECOND);
			if (sample == NULL)
				break;
			GstBuffer* gb = gst_sample_get_buffer(sample);
			GstMapInfo map;
			if (gb != NULL && gst_buffer_map(gb, &map, GST_MAP_READ)) {
				status_t err = fOwner->WriteChunk(map.data, map.size,
					info);
				gst_buffer_unmap(gb, &map);
				if (err != B_OK) {
					gst_sample_unref(sample);
					return err;
				}
			}
			gst_sample_unref(sample);
		}

		GstBus* bus = gst_element_get_bus(fPipeline);
		if (bus != NULL) {
			GstMessage* message = gst_bus_pop(bus);
			if (message != NULL) {
				if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
					gst_message_unref(message);
					gst_object_unref(bus);
					return B_ERROR;
				}
				gst_message_unref(message);
			}
			gst_object_unref(bus);
		}
		return B_OK;
	}

	BMediaEncoder*	fOwner;
	GstElement*		fPipeline;
	GstElement*		fAppSrc;
	GstElement*		fAppSink;
	status_t		fInitErr;
	int32			fCodecId;
	BMediaFormat	fRawFormat;
	uint32			fFrameStride;
	uint64			fNextPts;
};


BMediaEncoder::BMediaEncoder()
	:
	fImpl(new(std::nothrow) Impl(this))
{
}


BMediaEncoder::BMediaEncoder(const BMediaFormat& outputFormat)
	:
	fImpl(new(std::nothrow) Impl(this))
{
	if (fImpl != NULL)
		fImpl->SetTo(outputFormat);
}


BMediaEncoder::~BMediaEncoder()
{
	delete fImpl;
}


status_t
BMediaEncoder::InitCheck() const
{
	return fImpl != NULL ? fImpl->fInitErr : B_NO_MEMORY;
}


status_t
BMediaEncoder::SetTo(const BMediaFormat& outputFormat)
{
	if (fImpl == NULL)
		return B_NO_MEMORY;
	return fImpl->SetTo(outputFormat);
}


status_t
BMediaEncoder::SetFormat(BMediaFormat* inputFormat, BMediaFormat* outputFormat)
{
	if (inputFormat == NULL || outputFormat == NULL)
		return B_BAD_VALUE;
	// The raw input parameters live inside the encoded output format, so
	// adopting the caller's raw view keeps both sides consistent.
	if (outputFormat->IsEncodedAudio()) {
		if (!inputFormat->IsRawAudio())
			return B_BAD_VALUE;
		outputFormat->format.u.encoded_audio.output
			= inputFormat->format.u.raw_audio;
	} else if (outputFormat->IsEncodedVideo()) {
		if (!inputFormat->IsRawVideo())
			return B_BAD_VALUE;
		outputFormat->format.u.encoded_video.output
			= inputFormat->format.u.raw_video;
	} else {
		return B_BAD_VALUE;
	}
	return SetTo(*outputFormat);
}


status_t
BMediaEncoder::Encode(const void* buffer, int64 frameCount,
	media_encode_info* info)
{
	if (fImpl == NULL)
		return B_NO_MEMORY;
	return fImpl->Encode(buffer, frameCount, info);
}


status_t
BMediaEncoder::SetQuality(float quality)
{
	if (fImpl == NULL || fImpl->fInitErr != B_OK)
		return B_NO_INIT;
	ApplyEncodeQuality(fImpl->fPipeline, fImpl->fCodecId, quality);
	return B_OK;
}


status_t
BMediaEncoder::Flush()
{
	if (fImpl == NULL || fImpl->fInitErr != B_OK)
		return B_NO_INIT;
	return fImpl->_Drain(NULL);
}


status_t
BMediaEncoder::AddTrackInfo(uint32 /*code*/, const char* /*data*/,
	size_t /*size*/)
{
	// The GStreamer encoder elements own their header atoms; there is no
	// generic hook to inject arbitrary track data.
	return B_NOT_SUPPORTED;
}


// #pragma mark - BMediaBufferEncoder


BMediaBufferEncoder::BMediaBufferEncoder()
	:
	BMediaEncoder(),
	fBuffer(NULL),
	fBufferSize(0),
	fBufferUsed(0)
{
}


BMediaBufferEncoder::BMediaBufferEncoder(const BMediaFormat& outputFormat)
	:
	BMediaEncoder(outputFormat),
	fBuffer(NULL),
	fBufferSize(0),
	fBufferUsed(0)
{
}


status_t
BMediaBufferEncoder::EncodeToBuffer(void* outputBuffer, size_t* _size,
	const void* inputBuffer, int64 frameCount, media_encode_info* info)
{
	if (outputBuffer == NULL || _size == NULL || inputBuffer == NULL)
		return B_BAD_VALUE;
	fBuffer = outputBuffer;
	fBufferSize = (int32)*_size;
	fBufferUsed = 0;
	status_t err = Encode(inputBuffer, frameCount, info);
	*_size = fBufferUsed;
	fBuffer = NULL;
	fBufferSize = 0;
	return err;
}


status_t
BMediaBufferEncoder::WriteChunk(const void* buffer, size_t size,
	media_encode_info* /*info*/)
{
	if (fBuffer == NULL)
		return B_NO_INIT;
	if (fBufferUsed + size > (size_t)fBufferSize)
		return B_NO_MEMORY;
	memcpy((uint8*)fBuffer + fBufferUsed, buffer, size);
	fBufferUsed += size;
	return B_OK;
}
