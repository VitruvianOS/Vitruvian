/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "CortexMediaUnits.h"

#include <new>
#include <string.h>

#include <Autolock.h>

#include <media2/MediaFile.h>
#include <media2/MediaTrack.h>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>


namespace {


const CortexUnitInfo kCortexUnits[] = {
	{ "decoder", "Decoder", "codec" },
	{ "encoder", "Encoder", "codec" },
	{ "file-reader", "File Reader", "file" },
	{ "file-writer", "File Writer", "file" }
};


const int32 kCortexUnitCount = sizeof(kCortexUnits) / sizeof(kCortexUnits[0]);


static void
_FillRawDefaults(media_raw_audio_format* raw)
{
	raw->frame_rate = 48000.0f;
	raw->channel_count = 2;
	raw->format = media_raw_audio_format::B_AUDIO_FLOAT;
	raw->byte_order = B_MEDIA_HOST_ENDIAN;
	raw->buffer_size = 0;
}


static const char*
_SampleFormatName(uint32 format, uint32 byteOrder)
{
	const bool isLE = byteOrder != B_MEDIA_BIG_ENDIAN;
	switch (format) {
		case media_raw_audio_format::B_AUDIO_FLOAT:
			return isLE ? "F32LE" : "F32BE";
		case media_raw_audio_format::B_AUDIO_DOUBLE:
			return isLE ? "F64LE" : "F64BE";
		case media_raw_audio_format::B_AUDIO_SHORT:
			return isLE ? "S16LE" : "S16BE";
		case media_raw_audio_format::B_AUDIO_INT:
			return isLE ? "S32LE" : "S32BE";
		case media_raw_audio_format::B_AUDIO_UCHAR:
			return "U8";
		case media_raw_audio_format::B_AUDIO_CHAR:
			return "S8";
		default:
			return NULL;
	}
}


static GstCaps*
_RawAudioCaps(const media_raw_audio_format& raw)
{
	const char* sampleFormat = _SampleFormatName(raw.format, raw.byte_order);
	if (sampleFormat == NULL || raw.frame_rate <= 0.0f
			|| raw.channel_count == 0) {
		return NULL;
	}

	GstStructure* structure = gst_structure_new("audio/x-raw",
		"format", G_TYPE_STRING, sampleFormat,
		"layout", G_TYPE_STRING, "interleaved",
		"rate", G_TYPE_INT, (int)raw.frame_rate,
		"channels", G_TYPE_INT, (int)raw.channel_count,
		NULL);
	if (structure == NULL)
		return NULL;

	GstCaps* caps = gst_caps_new_empty();
	if (caps == NULL) {
		gst_structure_free(structure);
		return NULL;
	}
	gst_caps_append_structure(caps, structure);
	return caps;
}


static GstCaps*
_EncodedAudioCaps(int32 encoding)
{
	switch (encoding) {
		case B_CODEC_TYPE_MP3:
			return gst_caps_new_simple("audio/mpeg", "mpegversion",
				G_TYPE_INT, 1, NULL);
		case B_CODEC_TYPE_AAC:
			return gst_caps_new_simple("audio/mpeg", "mpegversion",
				G_TYPE_INT, 4, NULL);
		case B_CODEC_TYPE_OGG:
			return gst_caps_new_empty_simple("audio/ogg");
		case B_CODEC_TYPE_OPUS:
			return gst_caps_new_empty_simple("audio/opus");
		case B_CODEC_TYPE_FLAC:
			return gst_caps_new_empty_simple("audio/x-flac");
		case B_CODEC_TYPE_WAV:
			return gst_caps_new_empty_simple("audio/x-wav");
		default:
			return NULL;
	}
}


static void
_AppendBuffer(GstAppSrc* appSrc, const void* data, size_t size)
{
	if (appSrc == NULL || data == NULL || size == 0)
		return;

	GstBuffer* buffer = gst_buffer_new_allocate(NULL, size, NULL);
	if (buffer == NULL)
		return;

	GstMapInfo map;
	if (!gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
		gst_buffer_unref(buffer);
		return;
	}
	memcpy(map.data, data, size);
	gst_buffer_unmap(buffer, &map);
	gst_app_src_push_buffer(appSrc, buffer);
}


static size_t
_FillFromSink(GstAppSink* appSink, void* outBuffer, size_t outSize)
{
	if (appSink == NULL || outBuffer == NULL || outSize == 0)
		return 0;

	GstSample* sample = gst_app_sink_try_pull_sample(appSink, 0);
	if (sample == NULL)
		return 0;

	size_t filled = 0;
	GstBuffer* buffer = gst_sample_get_buffer(sample);
	GstMapInfo map;
	if (buffer != NULL && gst_buffer_map(buffer, &map, GST_MAP_READ)) {
		size_t copySize = map.size < outSize ? map.size : outSize;
		memcpy(outBuffer, map.data, copySize);
		filled = copySize;
		gst_buffer_unmap(buffer, &map);
	}
	gst_sample_unref(sample);
	return filled;
}


static void
_DecoderPadAdded(GstElement* /*decodebin*/, GstPad* pad, gpointer data)
{
	GstElement* convert = (GstElement*)data;
	if (convert == NULL)
		return;

	GstCaps* caps = gst_pad_get_current_caps(pad);
	if (caps == NULL)
		caps = gst_pad_query_caps(pad, NULL);
	if (caps == NULL || gst_caps_is_empty(caps)) {
		if (caps != NULL)
			gst_caps_unref(caps);
		return;
	}

	const GstStructure* structure = gst_caps_get_structure(caps, 0);
	const char* name = gst_structure_get_name(structure);
	bool isAudio = name != NULL && strncmp(name, "audio/", 6) == 0;
	gst_caps_unref(caps);
	if (!isAudio)
		return;

	GstPad* sinkPad = gst_element_get_static_pad(convert, "sink");
	if (sinkPad == NULL)
		return;
	if (!gst_pad_is_linked(sinkPad))
		gst_pad_link(pad, sinkPad);
	gst_object_unref(sinkPad);
}


} // anonymous namespace


/* static */ int32
CortexUnitCatalog::CountUnits()
{
	return kCortexUnitCount;
}


/* static */ const CortexUnitInfo*
CortexUnitCatalog::UnitInfoAt(int32 index)
{
	if (index < 0 || index >= kCortexUnitCount)
		return NULL;
	return &kCortexUnits[index];
}


/* static */ BMediaClient*
CortexUnitCatalog::CreateUnit(int32 index)
{
	BMediaClient* unit = NULL;
	switch (index) {
		case 0:
			unit = new(std::nothrow) CortexDecoderUnit();
			break;
		case 1:
			unit = new(std::nothrow) CortexEncoderUnit();
			break;
		case 2:
			unit = new(std::nothrow) CortexFileReaderUnit();
			break;
		case 3:
			unit = new(std::nothrow) CortexFileWriterUnit();
			break;
		default:
			return NULL;
	}

	if (unit == NULL)
		return NULL;
	if (unit->InitCheck() != B_OK) {
		delete unit;
		return NULL;
	}
	return unit;
}


// #pragma mark - Decoder


class CortexDecoderInput : public BMediaInput {
public:
								CortexDecoderInput()
									:
									BMediaConnection(B_MEDIA_INPUT,
										"encoded in"),
									BMediaInput("encoded in")
	{
		SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_ENCODED_AUDIO));

		BMediaFormat format;
		format.format.type = B_MEDIA_ENCODED_AUDIO;
		media_encoded_audio_format& encoded = format.format.u.encoded_audio;
		memset(&encoded, 0, sizeof(encoded));
		encoded.encoding = B_CODEC_TYPE_UNKNOWN;
		SetAcceptedFormat(format);
	}

protected:
	virtual	status_t			AcceptFormat(BMediaFormat* format)
	{
		if (format == NULL || !format->IsEncodedAudio())
			return B_BAD_VALUE;
		return B_OK;
	}
};


class CortexDecoderOutput : public BMediaOutput {
public:
								CortexDecoderOutput()
									:
									BMediaConnection(B_MEDIA_OUTPUT,
										"raw out"),
									BMediaOutput("raw out")
	{
		SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO));

		BMediaFormat format;
		format.SetToDefault();
		SetAcceptedFormat(format);
	}

protected:
	virtual	status_t			PrepareToConnect(BMediaFormat* format)
	{
		if (format == NULL || !format->IsRawAudio())
			return B_BAD_VALUE;
		media_raw_audio_format& raw = format->format.u.raw_audio;
		if (raw.frame_rate <= 0.0f || raw.channel_count == 0
				|| raw.format == 0 || raw.byte_order == 0) {
			media_raw_audio_format defaults;
			_FillRawDefaults(&defaults);
			if (raw.frame_rate <= 0.0f)
				raw.frame_rate = defaults.frame_rate;
			if (raw.channel_count == 0)
				raw.channel_count = defaults.channel_count;
			if (raw.format == 0)
				raw.format = defaults.format;
			if (raw.byte_order == 0)
				raw.byte_order = defaults.byte_order;
		}
		return B_OK;
	}

	virtual	status_t			FormatProposal(BMediaFormat* format)
	{
		return PrepareToConnect(format);
	}
};


CortexDecoderUnit::CortexDecoderUnit()
	:
	BMediaUnit("Decoder", B_MEDIA_FILTER),
	fInput(NULL),
	fOutput(NULL),
	fPipeline(NULL),
	fAppSrc(NULL),
	fAppSink(NULL)
{
	fInput = new(std::nothrow) CortexDecoderInput();
	if (fInput != NULL)
		RegisterInput(fInput);

	fOutput = new(std::nothrow) CortexDecoderOutput();
	if (fOutput != NULL)
		RegisterOutput(fOutput);
}


CortexDecoderUnit::~CortexDecoderUnit()
{
	_DisconnectPipeline();
}


void
CortexDecoderUnit::_ConnectPipeline()
{
	if (fPipeline != NULL || fInput == NULL || fOutput == NULL)
		return;
	if (!fInput->HasBinding() || !fOutput->HasBinding())
		return;

	GstElement* pipeline = gst_pipeline_new("cortex-decoder");
	GstElement* appSrc = gst_element_factory_make("appsrc", "src");
	GstElement* decodeBin = gst_element_factory_make("decodebin", "decode");
	GstElement* convert = gst_element_factory_make("audioconvert", "convert");
	GstElement* resample
		= gst_element_factory_make("audioresample", "resample");
	GstElement* appSink = gst_element_factory_make("appsink", "sink");
	if (pipeline == NULL || appSrc == NULL || decodeBin == NULL
			|| convert == NULL || resample == NULL || appSink == NULL) {
		if (pipeline != NULL)
			gst_object_unref(pipeline);
		if (appSrc != NULL)
			gst_object_unref(appSrc);
		if (decodeBin != NULL)
			gst_object_unref(decodeBin);
		if (convert != NULL)
			gst_object_unref(convert);
		if (resample != NULL)
			gst_object_unref(resample);
		if (appSink != NULL)
			gst_object_unref(appSink);
		return;
	}

	gst_bin_add_many(GST_BIN(pipeline), appSrc, decodeBin, convert, resample,
		appSink, NULL);
	if (!gst_element_link_many(appSrc, decodeBin, NULL)
			|| !gst_element_link_many(convert, resample, appSink, NULL)) {
		gst_object_unref(pipeline);
		return;
	}
	g_signal_connect(decodeBin, "pad-added", G_CALLBACK(_DecoderPadAdded),
		convert);

	g_object_set(appSrc, "format", GST_FORMAT_TIME, "block", FALSE, NULL);
	const BMediaFormat& inFormat = fInput->Format();
	if (inFormat.IsEncodedAudio()) {
		GstCaps* caps = _EncodedAudioCaps(
			inFormat.format.u.encoded_audio.encoding);
		if (caps != NULL) {
			g_object_set(appSrc, "caps", caps, NULL);
			gst_caps_unref(caps);
		}
	}

	g_object_set(appSink, "sync", FALSE, "max-buffers", (guint)4, NULL);
	const BMediaFormat& outFormat = fOutput->Format();
	if (outFormat.IsRawAudio()) {
		GstCaps* caps = _RawAudioCaps(outFormat.format.u.raw_audio);
		if (caps != NULL) {
			gst_app_sink_set_caps(GST_APP_SINK(appSink), caps);
			gst_caps_unref(caps);
		}
	}

	if (gst_element_set_state(pipeline, GST_STATE_PLAYING)
			== GST_STATE_CHANGE_FAILURE) {
		gst_object_unref(pipeline);
		return;
	}

	// Bin owns the elements; keep borrowed pointers for the app ports.
	fPipeline = pipeline;
	fAppSrc = appSrc;
	fAppSink = appSink;
}


void
CortexDecoderUnit::_DisconnectPipeline()
{
	if (fPipeline == NULL)
		return;

	GstElement* pipeline = (GstElement*)fPipeline;
	gst_element_set_state(pipeline, GST_STATE_NULL);
	fPipeline = NULL;
	fAppSrc = NULL;
	fAppSink = NULL;
	gst_object_unref(pipeline);
}


void
CortexDecoderUnit::ProcessCycle(uint32 frameCount)
{
	if (fInput == NULL || fOutput == NULL) {
		BMediaUnit::ProcessCycle(frameCount);
		return;
	}

	if (fPipeline == NULL) {
		_ConnectPipeline();
		if (fPipeline == NULL) {
			BMediaUnit::ProcessCycle(frameCount);
			return;
		}
	}

	size_t inSize = 0;
	void* inBuffer = BufferFor(fInput, &inSize);
	if (inBuffer != NULL && inSize > 0)
		_AppendBuffer((GstAppSrc*)fAppSrc, inBuffer, inSize);

	size_t outSize = 0;
	void* outBuffer = BufferFor(fOutput, &outSize);
	if (outBuffer == NULL || outSize == 0)
		return;

	size_t filled = _FillFromSink((GstAppSink*)fAppSink, outBuffer, outSize);
	if (filled < outSize)
		memset((uint8*)outBuffer + filled, 0, outSize - filled);
}


// #pragma mark - Encoder


class CortexEncoderInput : public BMediaInput {
public:
								CortexEncoderInput()
									:
									BMediaConnection(B_MEDIA_INPUT, "raw in"),
									BMediaInput("raw in")
	{
		SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO));

		BMediaFormat format;
		format.SetToDefault();
		SetAcceptedFormat(format);
	}

protected:
	virtual	status_t			AcceptFormat(BMediaFormat* format)
	{
		if (format == NULL || !format->IsRawAudio())
			return B_BAD_VALUE;
		media_raw_audio_format& raw = format->format.u.raw_audio;
		if (raw.frame_rate <= 0.0f || raw.channel_count == 0
				|| raw.format == 0 || raw.byte_order == 0) {
			media_raw_audio_format defaults;
			_FillRawDefaults(&defaults);
			if (raw.frame_rate <= 0.0f)
				raw.frame_rate = defaults.frame_rate;
			if (raw.channel_count == 0)
				raw.channel_count = defaults.channel_count;
			if (raw.format == 0)
				raw.format = defaults.format;
			if (raw.byte_order == 0)
				raw.byte_order = defaults.byte_order;
		}
		return B_OK;
	}
};


class CortexEncoderOutput : public BMediaOutput {
public:
								CortexEncoderOutput()
									:
									BMediaConnection(B_MEDIA_OUTPUT,
										"encoded out"),
									BMediaOutput("encoded out")
	{
		SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_ENCODED_AUDIO));
	}

protected:
	virtual	status_t			PrepareToConnect(BMediaFormat* format)
	{
		if (format == NULL || !format->IsEncodedAudio())
			return B_BAD_VALUE;
		media_encoded_audio_format& encoded = format->format.u.encoded_audio;
		if (encoded.encoding == B_CODEC_TYPE_UNKNOWN)
			encoded.encoding = B_CODEC_TYPE_WAV;
		if (encoded.output.frame_rate <= 0.0f
				|| encoded.output.channel_count == 0
				|| encoded.output.format == 0
				|| encoded.output.byte_order == 0) {
			media_raw_audio_format defaults;
			_FillRawDefaults(&defaults);
			if (encoded.output.frame_rate <= 0.0f)
				encoded.output.frame_rate = defaults.frame_rate;
			if (encoded.output.channel_count == 0)
				encoded.output.channel_count = defaults.channel_count;
			if (encoded.output.format == 0)
				encoded.output.format = defaults.format;
			if (encoded.output.byte_order == 0)
				encoded.output.byte_order = defaults.byte_order;
		}
		return B_OK;
	}

	virtual	status_t			FormatProposal(BMediaFormat* format)
	{
		return PrepareToConnect(format);
	}
};


CortexEncoderUnit::CortexEncoderUnit()
	:
	BMediaUnit("Encoder", B_MEDIA_FILTER),
	fInput(NULL),
	fOutput(NULL),
	fPipeline(NULL),
	fAppSrc(NULL),
	fAppSink(NULL)
{
	fInput = new(std::nothrow) CortexEncoderInput();
	if (fInput != NULL)
		RegisterInput(fInput);

	fOutput = new(std::nothrow) CortexEncoderOutput();
	if (fOutput != NULL)
		RegisterOutput(fOutput);
}


CortexEncoderUnit::~CortexEncoderUnit()
{
	_DisconnectPipeline();
}


status_t
CortexEncoderUnit::_ConnectPipeline(const BMediaFormat& rawFormat)
{
	if (fPipeline != NULL)
		return B_OK;
	if (!rawFormat.IsRawAudio() || fOutput == NULL)
		return B_BAD_VALUE;

	GstElement* pipeline = gst_pipeline_new("cortex-encoder");
	GstElement* appSrc = gst_element_factory_make("appsrc", "src");
	GstElement* convert = gst_element_factory_make("audioconvert", "convert");
	GstElement* resample
		= gst_element_factory_make("audioresample", "resample");
	GstElement* encoder = gst_element_factory_make("wavenc", "encode");
	GstElement* appSink = gst_element_factory_make("appsink", "sink");
	if (pipeline == NULL || appSrc == NULL || convert == NULL
			|| resample == NULL || encoder == NULL || appSink == NULL) {
		if (pipeline != NULL)
			gst_object_unref(pipeline);
		if (appSrc != NULL)
			gst_object_unref(appSrc);
		if (convert != NULL)
			gst_object_unref(convert);
		if (resample != NULL)
			gst_object_unref(resample);
		if (encoder != NULL)
			gst_object_unref(encoder);
		if (appSink != NULL)
			gst_object_unref(appSink);
		return B_ERROR;
	}

	gst_bin_add_many(GST_BIN(pipeline), appSrc, convert, resample, encoder,
		appSink, NULL);
	if (!gst_element_link_many(appSrc, convert, resample, encoder, appSink,
			NULL)) {
		gst_object_unref(pipeline);
		return B_ERROR;
	}

	g_object_set(appSrc, "format", GST_FORMAT_TIME, "block", FALSE, NULL);
	GstCaps* inCaps = _RawAudioCaps(rawFormat.format.u.raw_audio);
	if (inCaps != NULL) {
		g_object_set(appSrc, "caps", inCaps, NULL);
		gst_caps_unref(inCaps);
	}

	GstCaps* outCaps = gst_caps_new_empty_simple("audio/x-wav");
	if (outCaps != NULL) {
		gst_app_sink_set_caps(GST_APP_SINK(appSink), outCaps);
		gst_caps_unref(outCaps);
	}
	g_object_set(appSink, "sync", FALSE, "max-buffers", (guint)4, NULL);

	if (gst_element_set_state(pipeline, GST_STATE_PLAYING)
			== GST_STATE_CHANGE_FAILURE) {
		gst_object_unref(pipeline);
		return B_ERROR;
	}

	fPipeline = pipeline;
	fAppSrc = appSrc;
	fAppSink = appSink;
	return B_OK;
}


void
CortexEncoderUnit::_DisconnectPipeline()
{
	if (fPipeline == NULL)
		return;

	GstElement* pipeline = (GstElement*)fPipeline;
	GstAppSrc* appSrc = (GstAppSrc*)fAppSrc;
	if (appSrc != NULL)
		gst_app_src_end_of_stream(appSrc);
	gst_element_set_state(pipeline, GST_STATE_NULL);
	fPipeline = NULL;
	fAppSrc = NULL;
	fAppSink = NULL;
	gst_object_unref(pipeline);
}


void
CortexEncoderUnit::ProcessCycle(uint32 frameCount)
{
	if (fInput == NULL || fOutput == NULL) {
		BMediaUnit::ProcessCycle(frameCount);
		return;
	}

	if (fPipeline == NULL) {
		if (!fInput->HasBinding() || !fOutput->HasBinding()) {
			BMediaUnit::ProcessCycle(frameCount);
			return;
		}
		if (_ConnectPipeline(fInput->Format()) != B_OK) {
			BMediaUnit::ProcessCycle(frameCount);
			return;
		}
	}

	size_t inSize = 0;
	void* inBuffer = BufferFor(fInput, &inSize);
	if (inBuffer != NULL && inSize > 0)
		_AppendBuffer((GstAppSrc*)fAppSrc, inBuffer, inSize);

	size_t outSize = 0;
	void* outBuffer = BufferFor(fOutput, &outSize);
	if (outBuffer == NULL || outSize == 0)
		return;

	size_t filled = _FillFromSink((GstAppSink*)fAppSink, outBuffer, outSize);
	if (filled < outSize)
		memset((uint8*)outBuffer + filled, 0, outSize - filled);
}


// #pragma mark - File reader


class CortexFileReaderOutput : public BMediaOutput {
public:
								CortexFileReaderOutput(
									CortexFileReaderUnit* unit)
									:
									BMediaConnection(B_MEDIA_OUTPUT,
										"audio out"),
									BMediaOutput("audio out"),
									fUnit(unit)
	{
		SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO));

		BMediaFormat format;
		format.SetToDefault();
		SetAcceptedFormat(format);
	}

protected:
	virtual	status_t			PrepareToConnect(BMediaFormat* format)
	{
		if (fUnit != NULL)
			return fUnit->ProposeOutputFormat(format);
		return B_BAD_VALUE;
	}

	virtual	status_t			FormatProposal(BMediaFormat* format)
	{
		return PrepareToConnect(format);
	}

private:
			CortexFileReaderUnit*	fUnit;
};


CortexFileReaderUnit::CortexFileReaderUnit()
	:
	BMediaUnit("File Reader", B_MEDIA_PLAYER),
	fOutput(NULL),
	fFileLock("cortex file reader"),
	fFile(NULL),
	fTrack(NULL)
{
	memset(&fTrackFormat, 0, sizeof(fTrackFormat));

	fOutput = new(std::nothrow) CortexFileReaderOutput(this);
	if (fOutput != NULL)
		RegisterOutput(fOutput);
}


CortexFileReaderUnit::~CortexFileReaderUnit()
{
	_CloseFile();
}


status_t
CortexFileReaderUnit::SetFile(const entry_ref& ref)
{
	BAutolock _(fFileLock);
	_CloseFile();

	BMediaFile* file = new(std::nothrow) BMediaFile(&ref);
	if (file == NULL)
		return B_NO_MEMORY;
	if (file->InitCheck() != B_OK) {
		status_t status = file->InitCheck();
		delete file;
		return status;
	}

	BMediaTrack* track = file->TrackAt(0);
	if (track == NULL || track->InitCheck() != B_OK) {
		delete file;
		return B_ERROR;
	}

	BMediaFormat decoded;
	if (track->DecodedFormat(&decoded) != B_OK || !decoded.IsRawAudio()) {
		delete file;
		return B_UNSUPPORTED;
	}

	fFile = file;
	fTrack = track;
	fTrackFormat = decoded.format.u.raw_audio;
	return B_OK;
}


status_t
CortexFileReaderUnit::ProposeOutputFormat(BMediaFormat* format)
{
	if (format == NULL || !format->IsRawAudio())
		return B_BAD_VALUE;

	BAutolock _(fFileLock);
	media_raw_audio_format& raw = format->format.u.raw_audio;
	if (fTrack != NULL && fTrackFormat.frame_rate > 0.0f) {
		raw.frame_rate = fTrackFormat.frame_rate;
		raw.channel_count = fTrackFormat.channel_count;
		raw.format = fTrackFormat.format;
		raw.byte_order = fTrackFormat.byte_order;
		return B_OK;
	}

	if (raw.frame_rate <= 0.0f || raw.channel_count == 0 || raw.format == 0
			|| raw.byte_order == 0) {
		media_raw_audio_format defaults;
		_FillRawDefaults(&defaults);
		if (raw.frame_rate <= 0.0f)
			raw.frame_rate = defaults.frame_rate;
		if (raw.channel_count == 0)
			raw.channel_count = defaults.channel_count;
		if (raw.format == 0)
			raw.format = defaults.format;
		if (raw.byte_order == 0)
			raw.byte_order = defaults.byte_order;
	}
	return B_OK;
}


void
CortexFileReaderUnit::_CloseFile()
{
	delete fFile;
	fFile = NULL;
	fTrack = NULL;
	memset(&fTrackFormat, 0, sizeof(fTrackFormat));
}


void
CortexFileReaderUnit::ProcessCycle(uint32 frameCount)
{
	if (fOutput == NULL || !fOutput->HasBinding()) {
		BMediaUnit::ProcessCycle(frameCount);
		return;
	}

	size_t size = 0;
	void* buffer = BufferFor(fOutput, &size);
	if (buffer == NULL || size == 0)
		return;

	BAutolock _(fFileLock);
	const uint32 stride = (fTrackFormat.format
		& media_raw_audio_format::B_AUDIO_SIZE_MASK)
		* fTrackFormat.channel_count;
	if (fTrack == NULL || stride == 0) {
		memset(buffer, 0, size);
		return;
	}

	int64 frames = size / stride;
	int64 got = frames;
	status_t status = fTrack->ReadFrames(buffer, &got, NULL);
	if (status == B_LAST_BUFFER_ERROR) {
		bigtime_t zero = 0;
		fTrack->SeekToTime(&zero);
		got = frames;
		status = fTrack->ReadFrames(buffer, &got, NULL);
	}

	size_t filled = 0;
	if ((status == B_OK || status == B_LAST_BUFFER_ERROR) && got > 0)
		filled = (size_t)got * stride;
	if (filled > size)
		filled = size;
	if (filled < size)
		memset((uint8*)buffer + filled, 0, size - filled);
}


// #pragma mark - File writer


class CortexFileWriterInput : public BMediaInput {
public:
								CortexFileWriterInput()
									:
									BMediaConnection(B_MEDIA_INPUT, "audio in"),
									BMediaInput("audio in")
	{
		SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO));

		BMediaFormat format;
		format.SetToDefault();
		SetAcceptedFormat(format);
	}

protected:
	virtual	status_t			AcceptFormat(BMediaFormat* format)
	{
		if (format == NULL || !format->IsRawAudio())
			return B_BAD_VALUE;
		media_raw_audio_format& raw = format->format.u.raw_audio;
		if (raw.frame_rate <= 0.0f || raw.channel_count == 0
				|| raw.format == 0 || raw.byte_order == 0) {
			media_raw_audio_format defaults;
			_FillRawDefaults(&defaults);
			if (raw.frame_rate <= 0.0f)
				raw.frame_rate = defaults.frame_rate;
			if (raw.channel_count == 0)
				raw.channel_count = defaults.channel_count;
			if (raw.format == 0)
				raw.format = defaults.format;
			if (raw.byte_order == 0)
				raw.byte_order = defaults.byte_order;
		}
		return B_OK;
	}
};


CortexFileWriterUnit::CortexFileWriterUnit()
	:
	BMediaUnit("File Writer", B_MEDIA_RECORDER),
	fInput(NULL),
	fLock("cortex file writer"),
	fPendingOpen(false),
	fFile(NULL),
	fTrack(NULL)
{
	memset(&fFileFormat, 0, sizeof(fFileFormat));
	memset(&fTrackFormat, 0, sizeof(fTrackFormat));

	fInput = new(std::nothrow) CortexFileWriterInput();
	if (fInput != NULL)
		RegisterInput(fInput);
}


CortexFileWriterUnit::~CortexFileWriterUnit()
{
	_CloseOutput();
}


status_t
CortexFileWriterUnit::SetFile(const entry_ref& ref,
	const media_file_format& fileFormat)
{
	BAutolock _(fLock);
	_CloseOutput();
	fOutputRef = ref;
	fFileFormat = fileFormat;
	fPendingOpen = true;
	return B_OK;
}


status_t
CortexFileWriterUnit::_OpenOutput(const media_raw_audio_format& raw)
{
	if (fFile != NULL)
		_CloseOutput();
	if (raw.frame_rate <= 0.0f || raw.channel_count == 0)
		return B_BAD_VALUE;

	BMediaFile* file = new(std::nothrow) BMediaFile(&fOutputRef,
		&fFileFormat, 0);
	if (file == NULL)
		return B_NO_MEMORY;
	if (file->InitCheck() != B_OK) {
		status_t status = file->InitCheck();
		delete file;
		return status;
	}

	BMediaFormat output;
	output.format.type = B_MEDIA_RAW_AUDIO;
	output.format.u.raw_audio = raw;
	output.is_planar = false;

	BMediaTrack* track = file->CreateTrack(output);
	if (track == NULL || track->InitCheck() != B_OK) {
		delete file;
		return B_ERROR;
	}
	if (file->CommitHeader() != B_OK) {
		delete file;
		return B_ERROR;
	}

	fFile = file;
	fTrack = track;
	fTrackFormat = raw;
	fPendingOpen = false;
	return B_OK;
}


void
CortexFileWriterUnit::_CloseOutput()
{
	if (fFile != NULL) {
		fFile->CloseFile();
		delete fFile;
	}
	fFile = NULL;
	fTrack = NULL;
	memset(&fTrackFormat, 0, sizeof(fTrackFormat));
}


void
CortexFileWriterUnit::HandleStop(bigtime_t performanceTime)
{
	{
		BAutolock _(fLock);
		_CloseOutput();
		fPendingOpen = false;
	}
	BMediaUnit::HandleStop(performanceTime);
}


void
CortexFileWriterUnit::ProcessCycle(uint32 frameCount)
{
	if (fInput == NULL) {
		BMediaUnit::ProcessCycle(frameCount);
		return;
	}

	size_t size = 0;
	void* buffer = BufferFor(fInput, &size);
	if (buffer == NULL || size == 0)
		return;

	BAutolock _(fLock);
	if (fPendingOpen && fTrack == NULL && fInput->HasBinding()) {
		const BMediaFormat& format = fInput->Format();
		if (format.IsRawAudio())
			_OpenOutput(format.format.u.raw_audio);
	}

	const uint32 stride = (fTrackFormat.format
		& media_raw_audio_format::B_AUDIO_SIZE_MASK)
		* fTrackFormat.channel_count;
	if (fTrack == NULL || stride == 0)
		return;

	int64 frames = size / stride;
	if (frames > 0)
		fTrack->WriteFrames(buffer, frames, NULL);
}
