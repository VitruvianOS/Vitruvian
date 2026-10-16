/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/MediaDefs.h>

#include <stdio.h>
#include <string.h>

#include <gst/gst.h>

#include "MediaEncodeTables.h"


using namespace BPrivate::media;


static void
FillFileFormat(const EncodeFileFormat* format, media_file_format* outFormat)
{
	memset(outFormat, 0, sizeof(*outFormat));
	outFormat->capabilities = media_file_format::B_READABLE
		| media_file_format::B_WRITABLE;
	if (format->containerCaps == NULL)
		outFormat->capabilities |= media_file_format::B_KNOWS_RAW_AUDIO;

	strncpy(outFormat->pretty_name, format->prettyName,
		sizeof(outFormat->pretty_name) - 1);
	strncpy(outFormat->short_name, format->shortName,
		sizeof(outFormat->short_name) - 1);
	strncpy(outFormat->mime_type, format->mimeType,
		sizeof(outFormat->mime_type) - 1);
	strncpy(outFormat->file_extension, format->extension,
		sizeof(outFormat->file_extension) - 1);
}


static void
FillCodecInfo(const EncodeCodecInfo* codec, media_codec_info* outCodecInfo)
{
	memset(outCodecInfo, 0, sizeof(*outCodecInfo));
	outCodecInfo->id = codec->id;

	const char* pretty = NULL;
	GstElementFactory* factory = FindEncodeFactory(codec);
	if (factory != NULL) {
		pretty = gst_element_factory_get_metadata(factory,
			GST_ELEMENT_METADATA_LONGNAME);
		if (pretty == NULL)
			pretty = gst_plugin_feature_get_name(
				GST_PLUGIN_FEATURE(factory));
		gst_object_unref(factory);
	}
	if (pretty == NULL)
		pretty = codec->srcCaps;
	snprintf(outCodecInfo->pretty_name, sizeof(outCodecInfo->pretty_name),
		"%s", pretty);
	snprintf(outCodecInfo->short_name, sizeof(outCodecInfo->short_name),
		"%s", codec->factory != NULL ? codec->factory : "raw");
}


status_t
get_next_file_format(int32* cookie, media_file_format* outFormat)
{
	if (cookie == NULL || outFormat == NULL)
		return B_BAD_VALUE;

	while (true) {
		const EncodeFileFormat* format = EncodeFileFormatAt(*cookie);
		if (format == NULL)
			return B_BAD_INDEX;
		(*cookie)++;
		if (!IsEncodeFileFormatAvailable(format))
			continue;
		FillFileFormat(format, outFormat);
		return B_OK;
	}
}


status_t
get_next_encoder(int32* cookie, media_codec_info* outCodecInfo)
{
	if (cookie == NULL || outCodecInfo == NULL)
		return B_BAD_VALUE;

	while (true) {
		const EncodeCodecInfo* codec = EncodeCodecAt(*cookie);
		if (codec == NULL)
			return B_BAD_INDEX;
		(*cookie)++;
		if (!IsEncodeCodecAvailable(codec->id))
			continue;
		FillCodecInfo(codec, outCodecInfo);
		return B_OK;
	}
}


static bool
FileFormatAcceptsCodec(const EncodeFileFormat* format, int32 codecId)
{
	if (format == NULL)
		return false;
	for (int32 i = 0; i < format->codecCount; i++) {
		if (format->codecs[i] == codecId)
			return true;
	}
	return false;
}


static const EncodeFileFormat*
ResolveFileFormat(const media_file_format* fileFormat)
{
	if (fileFormat == NULL)
		return NULL;
	const EncodeFileFormat* format = FindEncodeFileFormat(
		fileFormat->short_name);
	if (format == NULL) {
		for (int32 i = 0; ; i++) {
			const EncodeFileFormat* candidate = EncodeFileFormatAt(i);
			if (candidate == NULL)
				break;
			if (strcmp(candidate->mimeType,
					fileFormat->mime_type) == 0) {
				format = candidate;
				break;
			}
		}
	}
	return format;
}


status_t
get_next_encoder(int32* cookie, const media_file_format* fileFormat,
	const media_format* inputFormat, media_format* outOutputFormat,
	media_codec_info* outCodecInfo)
{
	if (cookie == NULL || outCodecInfo == NULL || inputFormat == NULL)
		return B_BAD_VALUE;

	const EncodeFileFormat* format = ResolveFileFormat(fileFormat);
	const bool wantAudio = inputFormat->IsAudio();
	const bool wantVideo = inputFormat->IsVideo();
	if (!wantAudio && !wantVideo)
		return B_BAD_VALUE;

	while (true) {
		const EncodeCodecInfo* codec = EncodeCodecAt(*cookie);
		if (codec == NULL)
			return B_BAD_INDEX;
		(*cookie)++;
		if (codec->audio != wantAudio)
			continue;
		if (!IsEncodeCodecAvailable(codec->id))
			continue;
		if (fileFormat != NULL
				&& !FileFormatAcceptsCodec(format, codec->id)) {
			continue;
		}

		FillCodecInfo(codec, outCodecInfo);
		if (outOutputFormat != NULL) {
			outOutputFormat->Clear();
			if (wantAudio) {
				outOutputFormat->type = B_MEDIA_ENCODED_AUDIO;
				outOutputFormat->u.encoded_audio.output
					= inputFormat->u.raw_audio;
				outOutputFormat->u.encoded_audio.encoding = codec->id;
			} else {
				outOutputFormat->type = B_MEDIA_ENCODED_VIDEO;
				outOutputFormat->u.encoded_video.output
					= inputFormat->u.raw_video;
				outOutputFormat->u.encoded_video.encoding = codec->id;
			}
		}
		return B_OK;
	}
}
