/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "MediaEncodeTables.h"

#include <string.h>

#include <gst/pbutils/encoding-profile.h>


namespace BPrivate { namespace media {


namespace {


const int32 kOggCodecs[] = {
	B_CODEC_TYPE_OGG, B_CODEC_TYPE_OPUS, B_CODEC_TYPE_THEORA
};


const int32 kWebMCodecs[] = {
	B_CODEC_TYPE_VP8, B_CODEC_TYPE_VP9, B_CODEC_TYPE_OGG,
	B_CODEC_TYPE_OPUS
};


const int32 kMatroskaCodecs[] = {
	B_CODEC_TYPE_VP8, B_CODEC_TYPE_VP9, B_CODEC_TYPE_THEORA,
	B_CODEC_TYPE_OGG, B_CODEC_TYPE_OPUS, B_CODEC_TYPE_FLAC
};


const int32 kMp4Codecs[] = {
	B_CODEC_TYPE_H264, B_CODEC_TYPE_AAC
};


const int32 kWavCodecs[] = {
	B_CODEC_TYPE_WAV
};


const int32 kFlacCodecs[] = {
	B_CODEC_TYPE_FLAC
};


const int32 kMp3Codecs[] = {
	B_CODEC_TYPE_MP3
};


// Enumeration order is the menu order: audio first, then video.
const EncodeCodecInfo kCodecs[] = {
	{ B_CODEC_TYPE_WAV, NULL, { NULL, NULL, NULL },
		"audio/x-wav", true,
		B_ENCODE_QUALITY_NONE, NULL, 0.0f, 0.0f },
	{ B_CODEC_TYPE_MP3, "lamemp3enc", { NULL, NULL, NULL },
		"audio/mpeg, mpegversion=(int)1, layer=(int)3", true,
		B_ENCODE_QUALITY_RANGE, "quality", 0.0f, 10.0f },
	{ B_CODEC_TYPE_OGG, "vorbisenc", { NULL, NULL, NULL },
		"audio/x-vorbis", true,
		B_ENCODE_QUALITY_RANGE, "quality", -0.1f, 1.0f },
	{ B_CODEC_TYPE_OPUS, "opusenc", { NULL, NULL, NULL },
		"audio/x-opus", true,
		B_ENCODE_QUALITY_BITRATE, "bitrate", 6000.0f, 510000.0f },
	{ B_CODEC_TYPE_FLAC, "flacenc", { NULL, NULL, NULL },
		"audio/x-flac", true,
		B_ENCODE_QUALITY_COMPRESSION, "compression-level", 0.0f, 8.0f },
	{ B_CODEC_TYPE_AAC, "avenc_aac", { "voaacenc", "fdkaacenc", NULL },
		"audio/mpeg, mpegversion=(int)4", true,
		B_ENCODE_QUALITY_BITRATE, "bitrate", 8000.0f, 320000.0f },
	{ B_CODEC_TYPE_VP8, "vp8enc", { NULL, NULL, NULL },
		"video/x-vp8", false,
		B_ENCODE_QUALITY_QUANTIZER, "quantizer", 0.0f, 127.0f },
	{ B_CODEC_TYPE_VP9, "vp9enc", { NULL, NULL, NULL },
		"video/x-vp9", false,
		B_ENCODE_QUALITY_QUANTIZER, "quantizer", 0.0f, 255.0f },
	{ B_CODEC_TYPE_THEORA, "theoraenc", { NULL, NULL, NULL },
		"video/x-theora", false,
		B_ENCODE_QUALITY_RANGE, "quality", 0.0f, 63.0f },
	{ B_CODEC_TYPE_H264, "x264enc", { NULL, NULL, NULL },
		"video/x-h264", false,
		B_ENCODE_QUALITY_QUANTIZER, "quantizer", 0.0f, 51.0f }
};


const EncodeFileFormat kFileFormats[] = {
	{ "oggmux", { NULL, NULL }, "OGG", "ogg", "application/ogg",
		"Ogg", "application/ogg", kOggCodecs, 3 },
	{ "webmmux", { NULL, NULL }, "WEBM", "webm", "video/webm",
		"WebM", "video/webm", kWebMCodecs, 4 },
	{ "matroskamux", { NULL, NULL }, "MATROSKA", "mkv",
		"video/x-matroska", "Matroska", "video/x-matroska",
		kMatroskaCodecs, 6 },
	{ "qtmux", { "mp4mux", NULL }, "MP4", "mp4", "video/mp4",
		"MP4", "video/mp4", kMp4Codecs, 2 },
	{ "wavenc", { NULL, NULL }, "WAV", "wav", "audio/x-wav",
		"WAV", NULL, kWavCodecs, 1 },
	{ "flacenc", { NULL, NULL }, "FLAC", "flac", "audio/x-flac",
		"FLAC", NULL, kFlacCodecs, 1 },
	{ "lamemp3enc", { NULL, NULL }, "MP3", "mp3", "audio/mpeg",
		"MP3", NULL, kMp3Codecs, 1 }
};


} // namespace


const EncodeCodecInfo*
FindEncodeCodec(int32 id)
{
	for (size_t i = 0; i < sizeof(kCodecs) / sizeof(kCodecs[0]); i++) {
		if (kCodecs[i].id == id)
			return &kCodecs[i];
	}
	return NULL;
}


const EncodeFileFormat*
FindEncodeFileFormat(const char* shortName)
{
	if (shortName == NULL)
		return NULL;
	for (size_t i = 0; i < sizeof(kFileFormats) / sizeof(kFileFormats[0]); i++) {
		if (strcmp(kFileFormats[i].shortName, shortName) == 0)
			return &kFileFormats[i];
	}
	return NULL;
}


const EncodeFileFormat*
EncodeFileFormatAt(int32 index)
{
	if (index < 0 || (size_t)index >= sizeof(kFileFormats)
			/ sizeof(kFileFormats[0]))
		return NULL;
	return &kFileFormats[index];
}


const EncodeCodecInfo*
EncodeCodecAt(int32 index)
{
	if (index < 0 || (size_t)index >= sizeof(kCodecs)
			/ sizeof(kCodecs[0]))
		return NULL;
	return &kCodecs[index];
}


GstElementFactory*
FindEncodeFactory(const EncodeCodecInfo* codec)
{
	if (codec == NULL || codec->factory == NULL)
		return NULL;
	GstElementFactory* factory = gst_element_factory_find(codec->factory);
	for (size_t i = 0; factory == NULL && i < 3; i++) {
		if (codec->alternates[i] != NULL)
			factory = gst_element_factory_find(codec->alternates[i]);
	}
	return factory;	// caller owns the reference
}


GstElementFactory*
FindEncodeFactory(const EncodeFileFormat* format)
{
	if (format == NULL || format->factory == NULL)
		return NULL;
	GstElementFactory* factory = gst_element_factory_find(format->factory);
	for (size_t i = 0; factory == NULL && i < 2; i++) {
		if (format->alternates[i] != NULL)
			factory = gst_element_factory_find(format->alternates[i]);
	}
	return factory;	// caller owns the reference
}


bool
IsEncodeCodecAvailable(int32 id)
{
	const EncodeCodecInfo* codec = FindEncodeCodec(id);
	if (codec == NULL)
		return false;
	if (codec->factory == NULL)
		return true;	// raw passthrough always exists
	GstElementFactory* factory = FindEncodeFactory(codec);
	if (factory == NULL)
		return false;
	gst_object_unref(factory);
	return true;
}


bool
IsEncodeFileFormatAvailable(const EncodeFileFormat* format)
{
	if (format == NULL)
		return false;
	GstElementFactory* factory = FindEncodeFactory(format);
	if (factory == NULL)
		return false;
	gst_object_unref(factory);
	return true;
}


static GstElement*
_CreateElement(const char* factoryName, const char* elementName)
{
	GstElementFactory* factory = gst_element_factory_find(factoryName);
	if (factory == NULL)
		return NULL;
	GstElement* element = gst_element_factory_create(factory, elementName);
	gst_object_unref(factory);
	return element;
}


static GstEncodingProfile*
_CreateStreamProfile(const EncodeCodecInfo* codec)
{
	GstCaps* caps = gst_caps_from_string(codec->srcCaps);
	if (caps == NULL)
		return NULL;
	GstEncodingProfile* profile;
	if (codec->audio) {
		profile = (GstEncodingProfile*)gst_encoding_audio_profile_new(
			caps, NULL, NULL, 1);
	} else {
		profile = (GstEncodingProfile*)gst_encoding_video_profile_new(
			caps, NULL, NULL, 1);
	}
	gst_caps_unref(caps);
	return profile;
}


GstElement*
CreateEncodePipeline(const char* path, const EncodeFileFormat* format,
	const int32* codecIds, int32 count, GstCaps** rawCaps,
	GstElement** appSrcs)
{
	if (path == NULL || format == NULL || codecIds == NULL || rawCaps == NULL
			|| appSrcs == NULL || count <= 0 || count > 2)
		return NULL;
	if (format->containerCaps == NULL && count != 1)
		return NULL;

	const EncodeCodecInfo* codecs[2] = { NULL, NULL };
	for (int32 i = 0; i < count; i++) {
		codecs[i] = FindEncodeCodec(codecIds[i]);
		if (codecs[i] == NULL || rawCaps[i] == NULL)
			return NULL;
	}

	GstElement* pipeline = gst_pipeline_new("vitruvian-encode");
	GstElement* sink = _CreateElement("filesink", "sink");
	if (pipeline == NULL || sink == NULL) {
		if (pipeline != NULL)
			gst_object_unref(pipeline);
		if (sink != NULL)
			gst_object_unref(sink);
		return NULL;
	}
	GstBin* bin = GST_BIN(pipeline);
	g_object_set(sink, "location", path, "sync", FALSE, NULL);

	const char* elementNames[2] = { "src0", "src1" };
	GstElement* appSrcs_[2] = { NULL, NULL };
	bool ok = true;

	if (format->containerCaps != NULL) {
		GstCaps* containerCaps = gst_caps_from_string(format->containerCaps);
		GstEncodingContainerProfile* container
			= gst_encoding_container_profile_new("vitruvian-out", NULL,
				containerCaps, NULL);
		if (containerCaps != NULL)
			gst_caps_unref(containerCaps);
		GstElement* encoderBin = _CreateElement("encodebin", "enc");
		if (container == NULL || encoderBin == NULL) {
			if (container != NULL)
				gst_encoding_profile_unref(container);
			if (encoderBin != NULL)
				gst_object_unref(encoderBin);
			gst_object_unref(pipeline);
			return NULL;
		}
		for (int32 i = 0; i < count && ok; i++) {
			GstEncodingProfile* stream = _CreateStreamProfile(codecs[i]);
			if (stream == NULL)
				ok = false;
			else {
				gst_encoding_container_profile_add_profile(container,
					stream);
			}
		}
		g_object_set(encoderBin, "profile", container, NULL);
		gst_encoding_profile_unref(container);

		gst_bin_add_many(bin, encoderBin, sink, NULL);
		ok = ok && gst_element_link(encoderBin, sink);

		for (int32 i = 0; i < count && ok; i++) {
			appSrcs_[i] = gst_element_factory_make("appsrc",
				elementNames[i]);
			if (appSrcs_[i] == NULL) {
				ok = false;
				break;
			}
			g_object_set(appSrcs_[i],
				"caps", rawCaps[i],
				"format", GST_FORMAT_TIME,
				"is-live", FALSE,
				NULL);
			gst_bin_add(bin, appSrcs_[i]);
			ok = gst_element_link(appSrcs_[i], encoderBin);
		}
	} else {
		// Direct chain: the encoder element writes the stream headers
		// itself, so no muxer sits between it and the filesink.
		GstElement* appSrc = gst_element_factory_make("appsrc",
			elementNames[0]);
		GstElement* convert = _CreateElement("audioconvert", "convert");
		GstElement* encoder = NULL;
		if (codecs[0]->factory != NULL) {
			const EncodeCodecInfo* codec = codecs[0];
			encoder = _CreateElement(codec->factory, "encoder");
			for (size_t i = 0; encoder == NULL && i < 3; i++) {
				if (codec->alternates[i] != NULL) {
					encoder = _CreateElement(codec->alternates[i],
						"encoder");
				}
			}
			// A stream format without its encoder writes garbage.
			if (encoder == NULL)
				ok = false;
		}
		if (appSrc == NULL || convert == NULL)
			ok = false;
		if (ok) {
			g_object_set(appSrc,
				"caps", rawCaps[0],
				"format", GST_FORMAT_TIME,
				"is-live", FALSE,
				NULL);
			appSrcs_[0] = appSrc;
			if (encoder != NULL) {
				gst_bin_add_many(bin, appSrc, convert, encoder,
					sink, NULL);
				ok = gst_element_link_many(appSrc, convert, encoder,
					sink, NULL);
			} else {
				gst_bin_add_many(bin, appSrc, convert, sink,
					NULL);
				ok = gst_element_link_many(appSrc, convert, sink,
					NULL);
			}
		} else {
			if (appSrc != NULL)
				gst_object_unref(appSrc);
			if (convert != NULL)
				gst_object_unref(convert);
			if (encoder != NULL)
				gst_object_unref(encoder);
		}
	}

	if (!ok) {
		gst_element_set_state(pipeline, GST_STATE_NULL);
		gst_object_unref(pipeline);
		return NULL;
	}

	for (int32 i = 0; i < count; i++)
		appSrcs[i] = appSrcs_[i];
	return pipeline;
}


static void
_ApplyQualityToElement(GstElement* element, const EncodeCodecInfo* codec,
	float quality)
{
	GstElementFactory* factory = gst_element_get_factory(element);
	if (factory == NULL)
		return;
	const char* name = gst_plugin_feature_get_name(
		GST_PLUGIN_FEATURE(factory));
	bool matches = codec->factory != NULL && strcmp(name,
		codec->factory) == 0;
	for (size_t i = 0; !matches && i < 3; i++) {
		if (codec->alternates[i] != NULL)
			matches = strcmp(name, codec->alternates[i]) == 0;
	}
	if (!matches)
		return;

	GParamSpec* spec = g_object_class_find_property(
		G_OBJECT_GET_CLASS(element), codec->qualityProperty);
	if (spec == NULL)
		return;

	float span = codec->qualityMax - codec->qualityMin;
	float value;
	switch (codec->qualityKind) {
		case B_ENCODE_QUALITY_QUANTIZER:
		case B_ENCODE_QUALITY_RANGE:
			// Lower quantizer / range value means better quality.
			value = codec->qualityMax - quality * span;
			break;
		case B_ENCODE_QUALITY_BITRATE:
		case B_ENCODE_QUALITY_COMPRESSION:
			value = codec->qualityMin + quality * span;
			break;
		default:
			return;
	}

	if (G_IS_PARAM_SPEC_INT(spec)) {
		GParamSpecInt* specInt = G_PARAM_SPEC_INT(spec);
		int intValue = (int)(value + 0.5f);
		if (intValue < specInt->minimum)
			intValue = specInt->minimum;
		if (intValue > specInt->maximum)
			intValue = specInt->maximum;
		g_object_set(element, codec->qualityProperty, intValue, NULL);
	} else {
		GParamSpecFloat* specFloat = G_PARAM_SPEC_FLOAT(spec);
		if (value < specFloat->minimum)
			value = specFloat->minimum;
		if (value > specFloat->maximum)
			value = specFloat->maximum;
		g_object_set(element, codec->qualityProperty, value, NULL);
	}
}


void
ApplyEncodeQuality(GstElement* pipeline, int32 codecId, float quality)
{
	const EncodeCodecInfo* codec = FindEncodeCodec(codecId);
	if (pipeline == NULL || codec == NULL
			|| codec->qualityKind == B_ENCODE_QUALITY_NONE
			|| codec->factory == NULL) {
		return;
	}
	if (quality < 0.0f)
		quality = 0.0f;
	if (quality > 1.0f)
		quality = 1.0f;

	GstIterator* it = gst_bin_iterate_recurse(GST_BIN(pipeline));
	GValue item = G_VALUE_INIT;
	while (gst_iterator_next(it, &item) == GST_ITERATOR_OK) {
		GstElement* element = (GstElement*)g_value_get_object(&item);
		_ApplyQualityToElement(element, codec, quality);
		g_value_reset(&item);
	}
	gst_iterator_free(it);
}


} } // namespace BPrivate::media
