/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef _MEDIA2_MEDIA_ENCODE_TABLES_H
#define _MEDIA2_MEDIA_ENCODE_TABLES_H


#include <media2/MediaDefs.h>

#include <gst/gst.h>


namespace BPrivate { namespace media {


// Quality mapping styles for the known encoder elements. Every enumerated
// encoder carries one so SetQuality(0..1) lands on a real property.
enum encode_quality_kind {
	B_ENCODE_QUALITY_NONE = 0,
	B_ENCODE_QUALITY_QUANTIZER,		// int, lower is better
	B_ENCODE_QUALITY_RANGE,			// float property, lower is better
	B_ENCODE_QUALITY_BITRATE,		// int bps property, higher is better
	B_ENCODE_QUALITY_COMPRESSION	// int level, higher is better
};


struct EncodeCodecInfo {
	int32		id;
	const char*	factory;
	const char*	alternates[3];
	const char*	srcCaps;
	bool		audio;
	int32		qualityKind;
	const char*	qualityProperty;
	float		qualityMin;
	float		qualityMax;
};


struct EncodeFileFormat {
	const char*	factory;
	const char*	alternates[2];
	const char*	shortName;
	const char*	extension;
	const char*	mimeType;
	const char*	prettyName;
	// NULL means a direct pipeline (appsrc ! encoder ! filesink) instead
	// of encodebin plus a container profile.
	const char*	containerCaps;
	const int32*	codecs;
	int32		codecCount;
};


const EncodeCodecInfo*	FindEncodeCodec(int32 id);
const EncodeFileFormat*	FindEncodeFileFormat(const char* shortName);
const EncodeFileFormat*	EncodeFileFormatAt(int32 index);
const EncodeCodecInfo*	EncodeCodecAt(int32 index);

// First registered element among factory and its alternates; NULL when
// none of them is installed.
GstElementFactory*		FindEncodeFactory(const EncodeCodecInfo* codec);
GstElementFactory*		FindEncodeFactory(const EncodeFileFormat* format);

bool					IsEncodeCodecAvailable(int32 id);
bool					IsEncodeFileFormatAvailable(const EncodeFileFormat*);

// Shared writer construction, also compiled into the host unit test.
// Creates a pipeline that muxes 'count' tracks into 'path': appsrc per
// track (caps supplied by the caller), encodebin with a container profile
// for multi-stream formats, or a direct encoder chain for raw stream
// formats. On success appSrcs[0..count) receive borrowed appsrc pointers.
GstElement*				CreateEncodePipeline(const char* path,
									const EncodeFileFormat* format,
									const int32* codecIds, int32 count,
									GstCaps** rawCaps,
									GstElement** appSrcs);

// Walks the pipeline for the encoder element that serves 'codecId' and
// maps quality (0..1) onto its property. Safe to call repeatedly; the
// encoder only exists after the first buffer has negotiated caps.
void					ApplyEncodeQuality(GstElement* pipeline,
									int32 codecId, float quality);


} } // namespace BPrivate::media


#endif // _MEDIA2_MEDIA_ENCODE_TABLES_H
