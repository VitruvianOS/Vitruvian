/*
 * Copyright 2025-2026, Dario Casalinuovo. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/MediaDefs.h>

#include <stdio.h>
#include <string.h>

#include <gst/gst.h>

#include "GStreamerBackend.h"


namespace {


// Snapshots are captured the first time *cookie == 0 and freed when the
// caller iterates past the end (or never — leak is bounded by process
// lifetime since there's one snapshot per kind).
struct Snapshot {
	GList* list = NULL;	// owned; freed via gst_plugin_feature_list_free
	int    size = 0;

	~Snapshot()
	{
		if (list != NULL)
			gst_plugin_feature_list_free(list);
	}
};


static Snapshot sMuxers;
static Snapshot sEncoders;


static void
ResetSnapshot(Snapshot& s, GstElementFactoryListType type)
{
	if (s.list != NULL) {
		gst_plugin_feature_list_free(s.list);
		s.list = NULL;
		s.size = 0;
	}
	if (BPrivate::media::GStreamerBackend::GetInstance() == NULL)
		return;
	s.list = gst_element_factory_list_get_elements(type, GST_RANK_MARGINAL);
	for (GList* l = s.list; l != NULL; l = l->next)
		s.size++;
}


// Maps a GstStructure mime name (from the muxer's src-template caps) back to
// a media_codec_type for the common containers.
static int32
MimeToCodecType(const char* mime)
{
	if (mime == NULL)
		return B_CODEC_TYPE_UNKNOWN;
	if (strstr(mime, "audio/x-wav")   != NULL) return B_CODEC_TYPE_WAV;
	if (strstr(mime, "application/ogg") != NULL) return B_CODEC_TYPE_OGG;
	if (strstr(mime, "audio/x-flac")  != NULL) return B_CODEC_TYPE_FLAC;
	if (strstr(mime, "audio/mpeg")    != NULL) return B_CODEC_TYPE_MP3;
	if (strstr(mime, "video/quicktime") != NULL
		|| strstr(mime, "video/mp4")  != NULL) return B_CODEC_TYPE_AAC;
	if (strstr(mime, "audio/x-opus") != NULL)  return B_CODEC_TYPE_OPUS;
	return B_CODEC_TYPE_UNKNOWN;
}


// Best-effort: pull the first mime type from a factory's src-pad template.
static const char*
FirstSrcMime(GstElementFactory* factory)
{
	const GList* templates = gst_element_factory_get_static_pad_templates(factory);
	for (const GList* t = templates; t != NULL; t = t->next) {
		GstStaticPadTemplate* tmpl = (GstStaticPadTemplate*)t->data;
		if (tmpl->direction != GST_PAD_SRC)
			continue;
		GstCaps* caps = gst_static_caps_get(&tmpl->static_caps);
		if (caps == NULL || gst_caps_is_empty(caps)) {
			if (caps != NULL) gst_caps_unref(caps);
			continue;
		}
		GstStructure* s = gst_caps_get_structure(caps, 0);
		const char* mime = s != NULL ? gst_structure_get_name(s) : NULL;
		// gst_structure_get_name returns a const pointer into the structure's
		// arena. The caps unref below is fine because GStreamer caches the
		// quark-backed name string; safe to return.
		gst_caps_unref(caps);
		return mime;
	}
	return NULL;
}


} // anonymous namespace


status_t
get_next_file_format(int32* cookie, media_file_format* outFormat)
{
	if (cookie == NULL || outFormat == NULL)
		return B_BAD_VALUE;

	if (*cookie == 0)
		ResetSnapshot(sMuxers, GST_ELEMENT_FACTORY_TYPE_MUXER);
	if (*cookie < 0 || *cookie >= sMuxers.size)
		return B_BAD_INDEX;

	GList* node = g_list_nth(sMuxers.list, *cookie);
	if (node == NULL)
		return B_BAD_INDEX;
	GstElementFactory* factory = (GstElementFactory*)node->data;

	memset(outFormat, 0, sizeof(*outFormat));
	outFormat->capabilities = media_file_format::B_READABLE
		| media_file_format::B_WRITABLE
		| media_file_format::B_KNOWS_RAW_AUDIO;

	const char* pretty = gst_element_factory_get_metadata(factory,
		GST_ELEMENT_METADATA_LONGNAME);
	const char* shortname = gst_plugin_feature_get_name(
		GST_PLUGIN_FEATURE(factory));
	const char* mime = FirstSrcMime(factory);

	strncpy(outFormat->pretty_name,
		pretty != NULL ? pretty : (shortname != NULL ? shortname : "Unknown"),
		sizeof(outFormat->pretty_name) - 1);
	strncpy(outFormat->short_name,
		shortname != NULL ? shortname : "",
		sizeof(outFormat->short_name) - 1);
	strncpy(outFormat->mime_type, mime != NULL ? mime : "",
		sizeof(outFormat->mime_type) - 1);

	(*cookie)++;
	return B_OK;
}


status_t
get_next_encoder(int32* cookie, media_codec_info* outCodecInfo)
{
	if (cookie == NULL || outCodecInfo == NULL)
		return B_BAD_VALUE;

	if (*cookie == 0)
		ResetSnapshot(sEncoders, GST_ELEMENT_FACTORY_TYPE_ENCODER);
	if (*cookie < 0 || *cookie >= sEncoders.size)
		return B_BAD_INDEX;

	GList* node = g_list_nth(sEncoders.list, *cookie);
	if (node == NULL)
		return B_BAD_INDEX;
	GstElementFactory* factory = (GstElementFactory*)node->data;

	memset(outCodecInfo, 0, sizeof(*outCodecInfo));
	const char* pretty = gst_element_factory_get_metadata(factory,
		GST_ELEMENT_METADATA_LONGNAME);
	const char* shortname = gst_plugin_feature_get_name(
		GST_PLUGIN_FEATURE(factory));
	const char* mime = FirstSrcMime(factory);

	strncpy(outCodecInfo->pretty_name,
		pretty != NULL ? pretty : (shortname != NULL ? shortname : "Unknown"),
		sizeof(outCodecInfo->pretty_name) - 1);
	strncpy(outCodecInfo->short_name,
		shortname != NULL ? shortname : "",
		sizeof(outCodecInfo->short_name) - 1);
	outCodecInfo->id = MimeToCodecType(mime);

	(*cookie)++;
	return B_OK;
}


// 5-arg overload — filters by the requested file_format's codec id and by the
// input format's media kind (audio vs video). The output format is filled
// with an encoded format whose `encoding` matches the codec.
status_t
get_next_encoder(int32* cookie, const media_file_format* fileFormat,
	const media_format* inputFormat, media_format* outOutputFormat,
	media_codec_info* outCodecInfo)
{
	if (cookie == NULL || outCodecInfo == NULL || inputFormat == NULL)
		return B_BAD_VALUE;

	const bool wantAudio = inputFormat->IsAudio();
	const bool wantVideo = inputFormat->IsVideo();

	while (true) {
		media_codec_info ci;
		status_t err = get_next_encoder(cookie, &ci);
		if (err != B_OK)
			return err;

		// fileFormat filter: if supplied, require codec id consistency. The
		// fileFormat carries mime_type; we accept any encoder whose codec id
		// is non-unknown and (when fileFormat is given) compatible.
		if (fileFormat != NULL && ci.id == B_CODEC_TYPE_UNKNOWN)
			continue;

		// Media-kind filter: encoder factory metadata klass contains "Audio"
		// or "Video"; we approximate via the codec id.
		const bool encIsAudio = ci.id == B_CODEC_TYPE_MP3
			|| ci.id == B_CODEC_TYPE_AAC || ci.id == B_CODEC_TYPE_OGG
			|| ci.id == B_CODEC_TYPE_OPUS || ci.id == B_CODEC_TYPE_FLAC
			|| ci.id == B_CODEC_TYPE_WAV  || ci.id == B_CODEC_TYPE_PCM;
		if (wantAudio && !encIsAudio)
			continue;
		if (wantVideo && encIsAudio)
			continue;

		*outCodecInfo = ci;
		if (outOutputFormat != NULL) {
			outOutputFormat->Clear();
			if (wantAudio) {
				outOutputFormat->type = B_MEDIA_ENCODED_AUDIO;
				outOutputFormat->u.encoded_audio.output
					= inputFormat->u.raw_audio;
				outOutputFormat->u.encoded_audio.encoding = ci.id;
			} else if (wantVideo) {
				outOutputFormat->type = B_MEDIA_ENCODED_VIDEO;
				outOutputFormat->u.encoded_video.output
					= inputFormat->u.raw_video;
				outOutputFormat->u.encoded_video.encoding = ci.id;
			}
		}
		return B_OK;
	}
}
