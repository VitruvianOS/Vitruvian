/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <media2/MediaDecoder.h>

#include <new>
#include <string.h>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>


class BMediaDecoder::Impl {
public:
	Impl()
		:
		fPipeline(NULL),
		fAppSrc(NULL),
		fAppSink(NULL),
		fInitErr(B_NO_INIT),
		fWidth(0),
		fHeight(0)
	{
		memset(&fDecoded, 0, sizeof(fDecoded));
	}

	~Impl()
	{
		_Teardown();
	}

	status_t SetTo(const BMediaFormat& inputFormat)
	{
		_Teardown();
		fInitErr = B_NO_INIT;

		if (!inputFormat.IsEncodedVideo())
			return B_NOT_SUPPORTED;
		const media_encoded_video_format& enc
			= inputFormat.format.u.encoded_video;
		if (enc.encoding != B_CODEC_TYPE_MJPEG)
			return B_NOT_SUPPORTED;

		if (gst_is_initialized() == FALSE) {
			if (!gst_init_check(NULL, NULL, NULL))
				return B_NO_INIT;
		}

		GError* error = NULL;
		GstElement* pipeline = gst_parse_launch(
			"appsrc name=src is-live=true format=time do-timestamp=true"
			" ! jpegdec ! videoconvert ! video/x-raw,format=BGRx"
			" ! appsink name=sink sync=false max-buffers=2", &error);
		if (error != NULL)
			g_error_free(error);
		if (pipeline == NULL)
			return B_NOT_SUPPORTED;

		fAppSrc = gst_bin_get_by_name(GST_BIN(pipeline), "src");
		fAppSink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
		if (fAppSrc == NULL || fAppSink == NULL) {
			if (fAppSrc != NULL)
				gst_object_unref(fAppSrc);
			if (fAppSink != NULL)
				gst_object_unref(fAppSink);
			fAppSrc = fAppSink = NULL;
			gst_object_unref(pipeline);
			return B_NOT_SUPPORTED;
		}
		fPipeline = pipeline;

		GstCaps* caps = gst_caps_new_empty_simple("image/jpeg");
		g_object_set(fAppSrc, "caps", caps, NULL);
		gst_caps_unref(caps);

		// Without this the chain never produces output.
		gst_element_set_state(fPipeline, GST_STATE_PLAYING);

		fDecoded.type = B_MEDIA_RAW_VIDEO;
		media_raw_video_format& raw = fDecoded.u.raw_video;
		raw = enc.output;
		raw.color_space = B_RGB32;
		fWidth = raw.width;
		fHeight = raw.height;

		fInitErr = B_OK;
		return B_OK;
	}

	status_t Decode(const void* chunk, size_t size, void* out,
		size_t* ioSize, BMediaFormat* outFormat)
	{
		if (fInitErr != B_OK)
			return B_NO_INIT;
		if (chunk == NULL || size == 0 || out == NULL)
			return B_BAD_VALUE;

		GstBuffer* buffer = gst_buffer_new_allocate(NULL, size, NULL);
		if (buffer == NULL)
			return B_NO_MEMORY;
		GstMapInfo map;
		if (!gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
			gst_buffer_unref(buffer);
			return B_ERROR;
		}
		memcpy(map.data, chunk, size);
		gst_buffer_unmap(buffer, &map);

		if (gst_app_src_push_buffer(GST_APP_SRC(fAppSrc), buffer)
				!= GST_FLOW_OK) {
			return B_ERROR;
		}

		GstSample* sample = gst_app_sink_try_pull_sample(
			GST_APP_SINK(fAppSink), 200 * GST_MSECOND);
		if (sample == NULL)
			return B_TIMED_OUT;

		status_t status = _CopyOut(sample, out, ioSize);
		gst_sample_unref(sample);
		if (status == B_OK && outFormat != NULL)
			outFormat->format = fDecoded;
		return status;
	}

	status_t DecodedFormat(BMediaFormat* outFormat) const
	{
		if (fInitErr != B_OK)
			return B_NO_INIT;
		if (outFormat == NULL)
			return B_BAD_VALUE;
		outFormat->format = fDecoded;
		return B_OK;
	}

	status_t InitCheck() const
	{
		return fInitErr;
	}

private:
	status_t _CopyOut(GstSample* sample, void* out, size_t* ioSize)
	{
		GstBuffer* buffer = gst_sample_get_buffer(sample);
		GstCaps* caps = gst_sample_get_caps(sample);
		GstVideoInfo info;
		if (buffer == NULL || caps == NULL
			|| !gst_video_info_from_caps(&info, caps)) {
			return B_ERROR;
		}

		const uint32 width = GST_VIDEO_INFO_WIDTH(&info);
		const uint32 height = GST_VIDEO_INFO_HEIGHT(&info);
		const size_t tight = (size_t)width * 4;
		const size_t total = tight * height;
		if (ioSize != NULL && *ioSize < total)
			return B_BUFFER_OVERFLOW;

		GstVideoFrame frame;
		if (!gst_video_frame_map(&frame, &info, buffer, GST_MAP_READ))
			return B_ERROR;
		const uint8* src = (const uint8*)GST_VIDEO_FRAME_PLANE_DATA(&frame, 0);
		const int stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0);
		for (uint32 y = 0; y < height; y++)
			memcpy((uint8*)out + y * tight, src + (size_t)y * stride, tight);
		gst_video_frame_unmap(&frame);

		if (width != fWidth || height != fHeight) {
			fWidth = width;
			fHeight = height;
			fDecoded.u.raw_video.width = width;
			fDecoded.u.raw_video.height = height;
		}
		if (ioSize != NULL)
			*ioSize = total;
		return B_OK;
	}

	void _Teardown()
	{
		if (fPipeline != NULL)
			gst_element_set_state(fPipeline, GST_STATE_NULL);
		if (fAppSrc != NULL)
			gst_object_unref(fAppSrc);
		if (fAppSink != NULL)
			gst_object_unref(fAppSink);
		if (fPipeline != NULL)
			gst_object_unref(fPipeline);
		fPipeline = NULL;
		fAppSrc = NULL;
		fAppSink = NULL;
	}

	GstElement*		fPipeline;
	GstElement*		fAppSrc;
	GstElement*		fAppSink;
	status_t		fInitErr;
	media_format	fDecoded;
	uint32			fWidth;
	uint32			fHeight;
};


BMediaDecoder::BMediaDecoder()
	:
	fImpl(new(std::nothrow) Impl()),
	fOutputLimit((size_t)-1),
	fOutputSize(0)
{
}


BMediaDecoder::BMediaDecoder(const BMediaFormat& inputFormat)
	:
	fImpl(new(std::nothrow) Impl()),
	fOutputLimit((size_t)-1),
	fOutputSize(0)
{
	SetTo(inputFormat);
}


BMediaDecoder::~BMediaDecoder()
{
	delete fImpl;
}


status_t
BMediaDecoder::InitCheck() const
{
	return fImpl != NULL ? fImpl->InitCheck() : B_NO_MEMORY;
}


status_t
BMediaDecoder::SetTo(const BMediaFormat& inputFormat)
{
	return fImpl != NULL ? fImpl->SetTo(inputFormat) : B_NO_MEMORY;
}


status_t
BMediaDecoder::GetDecodedFormat(BMediaFormat* outFormat) const
{
	return fImpl != NULL ? fImpl->DecodedFormat(outFormat) : B_NO_MEMORY;
}


status_t
BMediaDecoder::Decode(void* outBuffer, int64* outFrameCount,
	BMediaFormat* outFormat)
{
	if (fImpl == NULL)
		return B_NO_MEMORY;
	if (outBuffer == NULL)
		return B_BAD_VALUE;

	const void* chunk = NULL;
	size_t chunkSize = 0;
	status_t status = GetNextChunk(&chunk, &chunkSize);
	if (status != B_OK)
		return status;

	size_t outSize = fOutputLimit;
	status = fImpl->Decode(chunk, chunkSize, outBuffer, &outSize, outFormat);
	if (status == B_OK)
		fOutputSize = outSize;
	if (status == B_OK && outFrameCount != NULL)
		*outFrameCount = 1;
	return status;
}


BMediaBufferDecoder::BMediaBufferDecoder()
	:
	BMediaDecoder(),
	fChunk(NULL),
	fChunkSize(0)
{
}


BMediaBufferDecoder::BMediaBufferDecoder(const BMediaFormat& inputFormat)
	:
	BMediaDecoder(inputFormat),
	fChunk(NULL),
	fChunkSize(0)
{
}


status_t
BMediaBufferDecoder::DecodeBuffer(const void* input, size_t inputSize,
	void* output, size_t* outputSize, BMediaFormat* outFormat)
{
	if (input == NULL || inputSize == 0 || output == NULL)
		return B_BAD_VALUE;
	if (InitCheck() != B_OK)
		return B_NO_INIT;

	fChunk = input;
	fChunkSize = inputSize;
	fOutputLimit = outputSize != NULL ? *outputSize : (size_t)-1;
	int64 frames = 0;
	status_t status = Decode(output, &frames, outFormat);
	fOutputLimit = (size_t)-1;
	fChunk = NULL;
	fChunkSize = 0;
	if (status == B_OK && outputSize != NULL)
		*outputSize = fOutputSize;
	return status;
}


status_t
BMediaBufferDecoder::GetNextChunk(const void** chunk, size_t* size)
{
	if (fChunk == NULL)
		return B_ERROR;
	*chunk = fChunk;
	*size = fChunkSize;
	fChunk = NULL;
	return B_OK;
}
