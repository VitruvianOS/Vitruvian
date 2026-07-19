/*
 * Copyright 2025-2026, Dario Casalinuovo. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include "GStreamerBackend.h"

#include <mutex>
#include <stdlib.h>
#include <string.h>

#include <gst/audio/audio.h>
#include <gst/pbutils/encoding-profile.h>


namespace BPrivate { namespace media {


static GStreamerBackend* sInstance = NULL;
static std::once_flag    sInitOnce;


GStreamerBackend::GStreamerBackend()
	:
	fPreferHwCodecs(true)
{
}


void
GStreamerBackend::SetPreferHardwareCodecs(bool prefer)
{
	fPreferHwCodecs = prefer;
}


GStreamerBackend::~GStreamerBackend()
{
}


GStreamerBackend*
GStreamerBackend::GetInstance()
{
	std::call_once(sInitOnce, [] {
		gst_init(NULL, NULL);
		sInstance = new GStreamerBackend();
	});
	return sInstance;
}


// #pragma mark - format conversions


void
GStreamerBackend::_GstAudioFormatStringToNative(const char* fmt,
	uint32* nativeFmt, uint32* byteOrder)
{
	struct Entry { const char* gst; uint32 native; uint32 bo; };
	static const Entry kTable[] = {
		{ "F32LE", media_raw_audio_format::B_AUDIO_FLOAT,  B_MEDIA_LITTLE_ENDIAN },
		{ "F32BE", media_raw_audio_format::B_AUDIO_FLOAT,  B_MEDIA_BIG_ENDIAN    },
		{ "F64LE", media_raw_audio_format::B_AUDIO_DOUBLE, B_MEDIA_LITTLE_ENDIAN },
		{ "F64BE", media_raw_audio_format::B_AUDIO_DOUBLE, B_MEDIA_BIG_ENDIAN    },
		{ "S16LE", media_raw_audio_format::B_AUDIO_SHORT,  B_MEDIA_LITTLE_ENDIAN },
		{ "S16BE", media_raw_audio_format::B_AUDIO_SHORT,  B_MEDIA_BIG_ENDIAN    },
		{ "S32LE", media_raw_audio_format::B_AUDIO_INT,    B_MEDIA_LITTLE_ENDIAN },
		{ "S32BE", media_raw_audio_format::B_AUDIO_INT,    B_MEDIA_BIG_ENDIAN    },
		{ "U8",    media_raw_audio_format::B_AUDIO_UCHAR,  0 },
		{ "S8",    media_raw_audio_format::B_AUDIO_CHAR,   0 },
	};
	if (fmt != NULL) {
		for (size_t i = 0; i < sizeof(kTable) / sizeof(kTable[0]); i++) {
			if (strcmp(fmt, kTable[i].gst) == 0) {
				*nativeFmt = kTable[i].native;
				*byteOrder = kTable[i].bo;
				return;
			}
		}
	}
	*nativeFmt = 0;
	*byteOrder = 0;
}


void
GStreamerBackend::_DefaultChannelPositions(int count,
	media_channel_position* out)
{
	for (int i = 0; i < B_AUDIO_MAX_CHANNELS; i++)
		out[i] = B_CHANNEL_NONE;
	static const media_channel_position kMono[]   = { B_CHANNEL_CENTER };
	static const media_channel_position kStereo[] = { B_CHANNEL_FRONT_LEFT,
		B_CHANNEL_FRONT_RIGHT };
	static const media_channel_position kQuad[]   = { B_CHANNEL_FRONT_LEFT,
		B_CHANNEL_FRONT_RIGHT, B_CHANNEL_REAR_LEFT, B_CHANNEL_REAR_RIGHT };
	static const media_channel_position k5_1[]    = { B_CHANNEL_FRONT_LEFT,
		B_CHANNEL_FRONT_RIGHT, B_CHANNEL_CENTER, B_CHANNEL_LFE,
		B_CHANNEL_REAR_LEFT, B_CHANNEL_REAR_RIGHT };
	static const media_channel_position k7_1[]    = { B_CHANNEL_FRONT_LEFT,
		B_CHANNEL_FRONT_RIGHT, B_CHANNEL_CENTER, B_CHANNEL_LFE,
		B_CHANNEL_REAR_LEFT, B_CHANNEL_REAR_RIGHT, B_CHANNEL_SIDE_LEFT,
		B_CHANNEL_SIDE_RIGHT };
	const media_channel_position* src = NULL;
	switch (count) {
		case 1: src = kMono;   break;
		case 2: src = kStereo; break;
		case 4: src = kQuad;   break;
		case 6: src = k5_1;    break;
		case 8: src = k7_1;    break;
		default:
			for (int i = 0; i < count && i < B_AUDIO_MAX_CHANNELS; i++)
				out[i] = B_CHANNEL_UNKNOWN;
			return;
	}
	for (int i = 0; i < count && i < B_AUDIO_MAX_CHANNELS; i++)
		out[i] = src[i];
}


void
GStreamerBackend::_GstMaskToChannelPositions(guint64 mask, int count,
	media_channel_position* out)
{
	struct Entry { guint64 bit; media_channel_position pos; };
	static const Entry kTable[] = {
		{ (guint64)1 << GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,            B_CHANNEL_FRONT_LEFT },
		{ (guint64)1 << GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,           B_CHANNEL_FRONT_RIGHT },
		{ (guint64)1 << GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,          B_CHANNEL_CENTER },
		{ (guint64)1 << GST_AUDIO_CHANNEL_POSITION_LFE1,                  B_CHANNEL_LFE },
		{ (guint64)1 << GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,             B_CHANNEL_REAR_LEFT },
		{ (guint64)1 << GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,            B_CHANNEL_REAR_RIGHT },
		{ (guint64)1 << GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER,  B_CHANNEL_FRONT_LEFT_OF_CENTER },
		{ (guint64)1 << GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER, B_CHANNEL_FRONT_RIGHT_OF_CENTER },
		{ (guint64)1 << GST_AUDIO_CHANNEL_POSITION_REAR_CENTER,           B_CHANNEL_REAR_CENTER },
		{ (guint64)1 << GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,             B_CHANNEL_SIDE_LEFT },
		{ (guint64)1 << GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT,            B_CHANNEL_SIDE_RIGHT },
	};
	int filled = 0;
	for (size_t i = 0; i < sizeof(kTable) / sizeof(kTable[0]); i++) {
		if ((mask & kTable[i].bit) != 0 && filled < count
				&& filled < B_AUDIO_MAX_CHANNELS) {
			out[filled++] = kTable[i].pos;
		}
	}
	while (filled < count && filled < B_AUDIO_MAX_CHANNELS)
		out[filled++] = B_CHANNEL_UNKNOWN;
	for (int i = (filled < B_AUDIO_MAX_CHANNELS ? filled : B_AUDIO_MAX_CHANNELS);
			i < B_AUDIO_MAX_CHANNELS; i++) {
		out[i] = B_CHANNEL_NONE;
	}
}


bool
GStreamerBackend::GstCapsToRawAudio(const GstCaps* caps, BMediaFormat* out)
{
	if (caps == NULL || out == NULL)
		return false;
	if (!gst_caps_is_fixed(caps))
		return false;

	GstStructure* s = gst_caps_get_structure(caps, 0);
	if (s == NULL || !gst_structure_has_name(s, "audio/x-raw"))
		return false;

	int rate = 0;
	int channels = 0;
	gst_structure_get_int(s, "rate",     &rate);
	gst_structure_get_int(s, "channels", &channels);
	const char* fmtStr = gst_structure_get_string(s, "format");
	const char* layout = gst_structure_get_string(s, "layout");

	out->format.type = B_MEDIA_RAW_AUDIO;
	media_raw_audio_format& raw = out->format.u.raw_audio;
	raw.frame_rate    = (float)rate;
	raw.channel_count = (uint32)channels;
	raw.buffer_size   = 0;
	_GstAudioFormatStringToNative(fmtStr, &raw.format, &raw.byte_order);
	out->is_planar = (layout != NULL && strcmp(layout, "planar") == 0);

	guint64 mask = 0;
	if (gst_structure_get(s, "channel-mask", GST_TYPE_BITMASK, &mask, NULL)
			&& mask != 0) {
		_GstMaskToChannelPositions(mask, channels, out->channel_positions);
	} else {
		_DefaultChannelPositions(channels, out->channel_positions);
	}

	return raw.format != 0;
}


bool
GStreamerBackend::GstCapsToEncodedAudio(const GstCaps* caps, BMediaFormat* out)
{
	if (caps == NULL || out == NULL)
		return false;
	GstStructure* s = gst_caps_get_structure(caps, 0);
	if (s == NULL)
		return false;
	out->format.type = B_MEDIA_ENCODED_AUDIO;
	memset(&out->format.u.encoded_audio, 0, sizeof(out->format.u.encoded_audio));
	int rate = 0, channels = 0;
	gst_structure_get_int(s, "rate", &rate);
	gst_structure_get_int(s, "channels", &channels);
	media_raw_audio_format& raw = out->format.u.encoded_audio.output;
	raw.frame_rate    = (float)rate;
	raw.channel_count = (uint32)channels;
	return true;
}


bool
GStreamerBackend::GstCapsToEncodedVideo(const GstCaps* caps, BMediaFormat* out)
{
	if (caps == NULL || out == NULL)
		return false;
	GstStructure* s = gst_caps_get_structure(caps, 0);
	if (s == NULL)
		return false;
	out->format.type = B_MEDIA_ENCODED_VIDEO;
	memset(&out->format.u.encoded_video, 0, sizeof(out->format.u.encoded_video));
	int width = 0, height = 0;
	gst_structure_get_int(s, "width",  &width);
	gst_structure_get_int(s, "height", &height);
	media_raw_video_format& raw = out->format.u.encoded_video.output;
	raw.width  = (uint32)width;
	raw.height = (uint32)height;
	return true;
}


// #pragma mark - pipelines


const char*
GStreamerBackend::_NativeAudioFormatToGst(uint32 nativeFmt, uint32 byteOrder)
{
	const bool isLE = byteOrder == B_MEDIA_LITTLE_ENDIAN;
	switch (nativeFmt) {
		case media_raw_audio_format::B_AUDIO_FLOAT:  return isLE ? "F32LE" : "F32BE";
		case media_raw_audio_format::B_AUDIO_DOUBLE: return isLE ? "F64LE" : "F64BE";
		case media_raw_audio_format::B_AUDIO_SHORT:  return isLE ? "S16LE" : "S16BE";
		case media_raw_audio_format::B_AUDIO_INT:    return isLE ? "S32LE" : "S32BE";
		case media_raw_audio_format::B_AUDIO_UCHAR:  return "U8";
		case media_raw_audio_format::B_AUDIO_CHAR:   return "S8";
	}
	return NULL;
}


guint64
GStreamerBackend::_ChannelPositionsToGstMask(const media_channel_position* pos,
	int count)
{
	guint64 mask = 0;
	for (int i = 0; i < count && i < B_AUDIO_MAX_CHANNELS; i++) {
		switch (pos[i]) {
			case B_CHANNEL_FRONT_LEFT:
				mask |= (guint64)1 << GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT; break;
			case B_CHANNEL_FRONT_RIGHT:
				mask |= (guint64)1 << GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT; break;
			case B_CHANNEL_CENTER:
				mask |= (guint64)1 << GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER; break;
			case B_CHANNEL_LFE:
				mask |= (guint64)1 << GST_AUDIO_CHANNEL_POSITION_LFE1; break;
			case B_CHANNEL_REAR_LEFT:
				mask |= (guint64)1 << GST_AUDIO_CHANNEL_POSITION_REAR_LEFT; break;
			case B_CHANNEL_REAR_RIGHT:
				mask |= (guint64)1 << GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT; break;
			case B_CHANNEL_FRONT_LEFT_OF_CENTER:
				mask |= (guint64)1 << GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER; break;
			case B_CHANNEL_FRONT_RIGHT_OF_CENTER:
				mask |= (guint64)1 << GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER; break;
			case B_CHANNEL_REAR_CENTER:
				mask |= (guint64)1 << GST_AUDIO_CHANNEL_POSITION_REAR_CENTER; break;
			case B_CHANNEL_SIDE_LEFT:
				mask |= (guint64)1 << GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT; break;
			case B_CHANNEL_SIDE_RIGHT:
				mask |= (guint64)1 << GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT; break;
			default: break;
		}
	}
	return mask;
}


GstCaps*
GStreamerBackend::RawAudioToGstCaps(const BMediaFormat& fmt)
{
	if (!fmt.IsRawAudio())
		return NULL;
	const media_raw_audio_format& raw = fmt.format.u.raw_audio;
	const char* fmtStr = _NativeAudioFormatToGst(raw.format, raw.byte_order);
	if (fmtStr == NULL)
		return NULL;
	GstCaps* caps = gst_caps_new_simple("audio/x-raw",
		"format",   G_TYPE_STRING, fmtStr,
		"rate",     G_TYPE_INT,    (int)raw.frame_rate,
		"channels", G_TYPE_INT,    (int)raw.channel_count,
		"layout",   G_TYPE_STRING, fmt.is_planar ? "non-interleaved" : "interleaved",
		NULL);
	guint64 mask = _ChannelPositionsToGstMask(fmt.channel_positions,
		(int)raw.channel_count);
	if (mask != 0) {
		gst_caps_set_simple(caps,
			"channel-mask", GST_TYPE_BITMASK, mask, NULL);
	}
	return caps;
}


// #pragma mark - raw video conversion


static const char*
ColorSpaceToGstFormat(uint32 cs)
{
	switch (cs) {
		case B_RGB32:    return "BGRx";
		case B_RGBA32:   return "BGRA";
		case B_YCbCr422: return "YUY2";
		case B_YUV420:   return "I420";
		default:         return NULL;
	}
}


static uint32
GstFormatToColorSpace(const char* g)
{
	if (g == NULL) return B_NO_COLOR_SPACE;
	if (strcmp(g, "BGRx") == 0 || strcmp(g, "BGRA") == 0) return B_RGB32;
	if (strcmp(g, "RGBA") == 0) return B_RGBA32;
	if (strcmp(g, "YUY2") == 0) return B_YCbCr422;
	if (strcmp(g, "I420") == 0) return B_YUV420;
	return B_NO_COLOR_SPACE;
}


bool
GStreamerBackend::GstCapsToRawVideo(const GstCaps* caps, BMediaFormat* out)
{
	if (caps == NULL || out == NULL || gst_caps_is_empty(caps))
		return false;
	const GstStructure* s = gst_caps_get_structure(caps, 0);
	if (s == NULL || strcmp(gst_structure_get_name(s), "video/x-raw") != 0)
		return false;
	out->format.type = B_MEDIA_RAW_VIDEO;
	media_raw_video_format& rv = out->format.u.raw_video;
	int w = 0, h = 0;
	gst_structure_get_int(s, "width",  &w);
	gst_structure_get_int(s, "height", &h);
	rv.width  = (uint32)w;
	rv.height = (uint32)h;
	const char* g = gst_structure_get_string(s, "format");
	rv.color_space = GstFormatToColorSpace(g);
	int frNum = 0, frDen = 1;
	gst_structure_get_fraction(s, "framerate", &frNum, &frDen);
	rv.field_rate = frDen != 0 ? (float)frNum / (float)frDen : 0.0f;
	int parNum = 1, parDen = 1;
	gst_structure_get_fraction(s, "pixel-aspect-ratio", &parNum, &parDen);
	rv.pixel_width_aspect  = (uint16)parNum;
	rv.pixel_height_aspect = (uint16)parDen;
	rv.orientation = B_VIDEO_TOP_LEFT_RIGHT;
	return true;
}


GstCaps*
GStreamerBackend::RawVideoToGstCaps(const BMediaFormat& fmt)
{
	if (!fmt.IsRawVideo())
		return NULL;
	const media_raw_video_format& rv = fmt.format.u.raw_video;
	const char* g = ColorSpaceToGstFormat(rv.color_space);
	if (g == NULL)
		return NULL;
	GstCaps* caps = gst_caps_new_simple("video/x-raw",
		"format",            G_TYPE_STRING, g,
		"width",             G_TYPE_INT,    (int)rv.width,
		"height",            G_TYPE_INT,    (int)rv.height,
		NULL);
	if (rv.field_rate > 0.0f) {
		gst_caps_set_simple(caps,
			"framerate", GST_TYPE_FRACTION, (int)(rv.field_rate + 0.5f), 1, NULL);
	}
	if (rv.pixel_width_aspect != 0 && rv.pixel_height_aspect != 0) {
		gst_caps_set_simple(caps,
			"pixel-aspect-ratio", GST_TYPE_FRACTION,
			(int)rv.pixel_width_aspect, (int)rv.pixel_height_aspect, NULL);
	}
	return caps;
}


// #pragma mark - encode profile mapping


struct CodecMime {
	media_codec_type	codec;
	const char*			audioCaps;
	const char*			containerCaps;
};

static const CodecMime kCodecMimeTable[] = {
	{ B_CODEC_TYPE_MP3,  "audio/mpeg, mpegversion=(int)1, layer=(int)3", "application/x-id3" },
	{ B_CODEC_TYPE_AAC,  "audio/mpeg, mpegversion=(int)4",               "video/quicktime, variant=(string)iso" },
	{ B_CODEC_TYPE_OGG,  "audio/x-vorbis",                               "application/ogg" },
	{ B_CODEC_TYPE_OPUS, "audio/x-opus",                                 "application/ogg" },
	{ B_CODEC_TYPE_FLAC, "audio/x-flac",                                 "audio/x-flac" },
	{ B_CODEC_TYPE_WAV,  "audio/x-raw",                                  "audio/x-wav" },
	{ B_CODEC_TYPE_PCM,  "audio/x-raw",                                  "audio/x-wav" }
};


static const CodecMime*
LookupCodecMime(int32 encoding)
{
	for (size_t i = 0; i < sizeof(kCodecMimeTable) / sizeof(kCodecMimeTable[0]); i++) {
		if (kCodecMimeTable[i].codec == (media_codec_type)encoding)
			return &kCodecMimeTable[i];
	}
	return &kCodecMimeTable[5];	// default to WAV
}


static GstEncodingProfile*
BuildEncodingProfile(const BMediaFormat& fmt)
{
	if (!fmt.IsEncodedAudio())
		return NULL;
	const media_encoded_audio_format& enc = fmt.format.u.encoded_audio;
	const CodecMime* m = LookupCodecMime(enc.encoding);

	GstCaps* containerCaps = gst_caps_from_string(m->containerCaps);
	GstEncodingContainerProfile* container = gst_encoding_container_profile_new(
		"vitruvian-out", NULL, containerCaps, NULL);
	gst_caps_unref(containerCaps);

	GstCaps* audioCaps = gst_caps_from_string(m->audioCaps);
	GstEncodingAudioProfile* ap = gst_encoding_audio_profile_new(audioCaps,
		NULL, NULL, 1);
	gst_caps_unref(audioCaps);

	gst_encoding_container_profile_add_profile(container,
		(GstEncodingProfile*)ap);
	return (GstEncodingProfile*)container;
}


// #pragma mark - hardware encoder rank boosting


namespace {
	struct EncoderRank { const char* name; GstRank original; bool boosted; };
	// In priority order — first matching factory wins inside each codec.
	EncoderRank kHwEncoders[] = {
		{ "v4l2slh264enc", (GstRank)0, false },
		{ "v4l2h264enc",   (GstRank)0, false },
		{ "vaapih264enc",  (GstRank)0, false },
		{ "v4l2slh265enc", (GstRank)0, false },
		{ "v4l2h265enc",   (GstRank)0, false },
		{ "vaapih265enc",  (GstRank)0, false },
	};
	EncoderRank kHwDecoders[] = {
		{ "v4l2slh264dec", (GstRank)0, false },
		{ "v4l2h264dec",   (GstRank)0, false },
		{ "vaapih264dec",  (GstRank)0, false },
		{ "v4l2slh265dec", (GstRank)0, false },
		{ "v4l2h265dec",   (GstRank)0, false },
		{ "vaapih265dec",  (GstRank)0, false },
	};
}


void
GStreamerBackend::_BoostHwEncoderRanks()
{
	GstRegistry* reg = gst_registry_get();
	for (size_t i = 0; i < sizeof(kHwEncoders) / sizeof(kHwEncoders[0]); i++) {
		GstPluginFeature* feat = gst_registry_lookup_feature(reg,
			kHwEncoders[i].name);
		if (feat == NULL)
			continue;
		kHwEncoders[i].original = gst_plugin_feature_get_rank(feat);
		gst_plugin_feature_set_rank(feat,
			(GstRank)(GST_RANK_PRIMARY + 10));
		kHwEncoders[i].boosted = true;
		gst_object_unref(feat);
	}
}


void
GStreamerBackend::_RestoreEncoderRanks()
{
	GstRegistry* reg = gst_registry_get();
	for (size_t i = 0; i < sizeof(kHwEncoders) / sizeof(kHwEncoders[0]); i++) {
		if (!kHwEncoders[i].boosted)
			continue;
		GstPluginFeature* feat = gst_registry_lookup_feature(reg,
			kHwEncoders[i].name);
		if (feat == NULL)
			continue;
		gst_plugin_feature_set_rank(feat, kHwEncoders[i].original);
		kHwEncoders[i].boosted = false;
		gst_object_unref(feat);
	}
}


void
GStreamerBackend::_BoostHwDecoderRanks()
{
	GstRegistry* reg = gst_registry_get();
	for (size_t i = 0; i < sizeof(kHwDecoders) / sizeof(kHwDecoders[0]); i++) {
		GstPluginFeature* feat = gst_registry_lookup_feature(reg,
			kHwDecoders[i].name);
		if (feat == NULL)
			continue;
		kHwDecoders[i].original = gst_plugin_feature_get_rank(feat);
		gst_plugin_feature_set_rank(feat,
			(GstRank)(GST_RANK_PRIMARY + 10));
		kHwDecoders[i].boosted = true;
		gst_object_unref(feat);
	}
}


void
GStreamerBackend::_RestoreDecoderRanks()
{
	GstRegistry* reg = gst_registry_get();
	for (size_t i = 0; i < sizeof(kHwDecoders) / sizeof(kHwDecoders[0]); i++) {
		if (!kHwDecoders[i].boosted)
			continue;
		GstPluginFeature* feat = gst_registry_lookup_feature(reg,
			kHwDecoders[i].name);
		if (feat == NULL)
			continue;
		gst_plugin_feature_set_rank(feat, kHwDecoders[i].original);
		kHwDecoders[i].boosted = false;
		gst_object_unref(feat);
	}
}


// #pragma mark - encode pipeline


GstElement*
GStreamerBackend::CreateEncodePipeline(const char* path,
	const BMediaFormat& outputFormat)
{
	if (path == NULL)
		return NULL;
	const char* swForcedEnv = getenv("VITRUVIAN_MEDIA_SW_ONLY");
	const bool boost = fPreferHwCodecs && swForcedEnv == NULL;
	if (boost)
		_BoostHwEncoderRanks();

	GstEncodingProfile* profile = BuildEncodingProfile(outputFormat);
	if (profile == NULL) {
		if (boost)
			_RestoreEncoderRanks();
		return NULL;
	}

	GError* err = NULL;
	GstElement* pipeline = gst_parse_launch(
		"appsrc name=src ! encodebin name=enc ! filesink name=sink",
		&err);
	if (err != NULL) {
		g_error_free(err);
		err = NULL;
	}
	if (pipeline == NULL) {
		gst_encoding_profile_unref(profile);
		if (boost)
			_RestoreEncoderRanks();
		return NULL;
	}

	GstElement* enc = gst_bin_get_by_name(GST_BIN(pipeline), "enc");
	if (enc != NULL) {
		g_object_set(enc, "profile", profile, NULL);
		gst_object_unref(enc);
	}
	GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
	if (sink != NULL) {
		g_object_set(sink, "location", path, NULL);
		gst_object_unref(sink);
	}

	gst_encoding_profile_unref(profile);

	if (boost)
		_RestoreEncoderRanks();

	return pipeline;
}


// #pragma mark - decode pipeline (defined later for symbol ordering)


GstElement*
GStreamerBackend::CreateDecodePipeline(const char* uri)
{
	// uri may be a filesystem path or a gst URI (file:///...). decodebin
	// handles both via uridecodebin-style auto-plugging; we use the
	// simpler filesrc+decodebin chain for explicit error reporting and
	// build an appsink dynamically when a pad is exposed.
	const char* swForcedEnv = getenv("VITRUVIAN_MEDIA_SW_ONLY");
	const bool boost = fPreferHwCodecs && swForcedEnv == NULL;
	if (boost)
		_BoostHwDecoderRanks();

	gchar* desc = g_strdup_printf(
		"filesrc location=\"%s\" ! decodebin name=dec", uri);
	GError* err = NULL;
	GstElement* pipeline = gst_parse_launch(desc, &err);
	g_free(desc);
	if (err != NULL) {
		g_error_free(err);
	}

	if (boost)
		_RestoreDecoderRanks();

	return pipeline;
}


} } // namespace BPrivate::media
