/*
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_MEDIA_DEFS_H
#define _MEDIA2_MEDIA_DEFS_H


#include <SupportDefs.h>
#include <GraphicsDefs.h>


// Values pinned to match legacy <MediaDefs.h>.
enum media_type {
	B_MEDIA_NO_TYPE         = -1,
	B_MEDIA_UNKNOWN_TYPE    = 0,
	B_MEDIA_RAW_AUDIO       = 1,
	B_MEDIA_RAW_VIDEO       = 2,
	B_MEDIA_VBL             = 3,	// reserved (legacy parity)
	B_MEDIA_TIMECODE        = 4,	// reserved (legacy parity)
	B_MEDIA_MIDI            = 5,
	B_MEDIA_TEXT            = 6,	// reserved (legacy parity)
	B_MEDIA_HTML            = 7,	// reserved (legacy parity)
	B_MEDIA_MULTISTREAM     = 8,	// reserved (legacy parity)
	B_MEDIA_PARAMETERS      = 9,
	B_MEDIA_ENCODED_AUDIO   = 10,
	B_MEDIA_ENCODED_VIDEO   = 11,
	B_MEDIA_PRIVATE         = 90000,
	B_MEDIA_FIRST_USER_TYPE = 100000
};


// Values pinned to match legacy <MediaDefs.h>.
enum {
	B_MEDIA_BIG_ENDIAN     = 1,
	B_MEDIA_LITTLE_ENDIAN  = 2,
#if B_HOST_IS_LENDIAN
	B_MEDIA_HOST_ENDIAN    = B_MEDIA_LITTLE_ENDIAN
#else
	B_MEDIA_HOST_ENDIAN    = B_MEDIA_BIG_ENDIAN
#endif
};


struct media_raw_audio_format {
	enum {
		B_AUDIO_FLOAT      = 0x24,
		B_AUDIO_DOUBLE     = 0x28,
		B_AUDIO_INT        = 0x04,
		B_AUDIO_SHORT      = 0x02,
		B_AUDIO_UCHAR      = 0x11,
		B_AUDIO_CHAR       = 0x01,
		B_AUDIO_SIZE_MASK  = 0x0f
	};

	float		frame_rate;
	uint32		channel_count;
	uint32		format;
	uint32		byte_order;
	size_t		buffer_size;

	static const media_raw_audio_format& wildcard;
};


// color_space is inherited from <GraphicsDefs.h> (included above).
// We use the canonical Haiku values: B_RGB32=0x0008, B_RGBA32=0x2008,
// B_YCbCr422=0x4000, B_YUV420=0x4024.

// Video orientation (legacy MediaDefs.h parity).
enum {
	B_VIDEO_TOP_LEFT_RIGHT = 0,
	B_VIDEO_BOTTOM_LEFT_RIGHT = 1
};


struct media_raw_video_format {
	float		field_rate;
	uint32		interlace;
	uint32		first_active;
	uint32		last_active;
	uint32		orientation;
	uint16		pixel_width_aspect;
	uint16		pixel_height_aspect;
	uint32		width;
	uint32		height;
	uint32		color_space;

	struct video_display_info {
		uint32	format;
		uint32	bytes_per_row;
		uint32	line_width;
		uint32	line_count;
		uint32	pixel_offset;
		uint32	line_offset;
	}			display;

	static const media_raw_video_format& wildcard;
};


enum media_codec_type {
	B_CODEC_TYPE_UNKNOWN = 0,
	B_CODEC_TYPE_PCM     = 1,	// raw PCM in a WAV container
	B_CODEC_TYPE_MP3     = 2,
	B_CODEC_TYPE_AAC     = 3,
	B_CODEC_TYPE_OGG     = 4,	// Vorbis-in-Ogg
	B_CODEC_TYPE_OPUS    = 5,	// Opus-in-Ogg
	B_CODEC_TYPE_FLAC    = 6,
	B_CODEC_TYPE_WAV     = 7
};


struct media_encoded_audio_format {
	media_raw_audio_format	output;
	int32					encoding;	// media_codec_type value
	float					bit_rate;
	size_t					frame_size;
};


struct media_encoded_video_format {
	media_raw_video_format	output;
	float					avg_bit_rate;
	float					max_bit_rate;
	int32					encoding;
	size_t					frame_size;
};


class media_format {
public:
	media_type	type;

	union {
		media_raw_audio_format		raw_audio;
		media_raw_video_format		raw_video;
		media_encoded_audio_format	encoded_audio;
		media_encoded_video_format	encoded_video;
		char						_reserved_[256];
	} u;

	inline bool IsAudio() const {
		return type == B_MEDIA_RAW_AUDIO || type == B_MEDIA_ENCODED_AUDIO;
	}
	inline bool IsVideo() const {
		return type == B_MEDIA_RAW_VIDEO || type == B_MEDIA_ENCODED_VIDEO;
	}
	inline void Clear() {
		type = B_MEDIA_NO_TYPE;
		for (size_t i = 0; i < sizeof(u); i++)
			((char*)&u)[i] = 0;
	}
	inline uint32 Width() const {
		if (type == B_MEDIA_RAW_VIDEO)     return u.raw_video.width;
		if (type == B_MEDIA_ENCODED_VIDEO) return u.encoded_video.output.width;
		return 0;
	}
	inline uint32 Height() const {
		if (type == B_MEDIA_RAW_VIDEO)     return u.raw_video.height;
		if (type == B_MEDIA_ENCODED_VIDEO) return u.encoded_video.output.height;
		return 0;
	}
	inline uint32& Width() {
		if (type == B_MEDIA_ENCODED_VIDEO) return u.encoded_video.output.width;
		return u.raw_video.width;
	}
	inline uint32& Height() {
		if (type == B_MEDIA_ENCODED_VIDEO) return u.encoded_video.output.height;
		return u.raw_video.height;
	}
};


extern const media_raw_audio_format media_raw_audio_format_wildcard;
extern const media_raw_video_format media_raw_video_format_wildcard;


// Per-type media header substructs. We don't decode video frames in media2
// directly yet; structs exist so apps can build/inspect headers without
// pulling legacy.
struct media_audio_header {
	uint32	field_flags;
	uint32	_reserved_[8];
};
struct media_video_header {
	uint32	field_flags;
	uint32	_reserved_[16];
};
struct media_encoded_audio_header {
	uint32	field_flags;
	uint32	_reserved_[8];
};
struct media_encoded_video_header {
	uint32	field_flags;
	uint32	_reserved_[16];
};


// Per-buffer flags carried in media_header::flags. Values match legacy.
enum {
	B_MEDIA_KEY_FRAME = 0x01
};


// Codec descriptor — used by BMediaFile::CreateTrack and BMediaTrack info.
// Trimmed from legacy: kept the fields apps actually reference.
struct media_codec_info {
	char	pretty_name[96];
	char	short_name[32];
	int32	id;				// media_codec_type value
	uint32	sub_id;
	uint32	_reserved_[16];
};


// File-container descriptor.
struct media_file_format {
	enum {
		B_KNOWS_RAW_VIDEO   = 0x01,
		B_KNOWS_RAW_AUDIO   = 0x02,
		B_KNOWS_OTHER       = 0x10000,
		B_READABLE          = 0x10000000,
		B_WRITABLE          = 0x20000000
	};
	uint32	capabilities;
	int32	family;			// reserved
	int32	version;
	char	mime_type[64];
	char	pretty_name[64];
	char	short_name[32];
	char	file_extension[8];
	uint32	_reserved_[16];
};


// File-system convenience: returns a populated array of known file formats
// the GStreamer backend supports (declared here so callers don't pull in
// GStreamerBackend.h). Implementation in MediaFormats.cpp (Batch C.2b).
status_t	get_next_file_format(int32* cookie, media_file_format* outFormat);
status_t	get_next_encoder(int32* cookie, media_codec_info* outCodecInfo);
status_t	get_next_encoder(int32* cookie, const media_file_format* fileFormat,
				const media_format* inputFormat, media_format* outOutputFormat,
				media_codec_info* outCodecInfo);


#endif // _MEDIA2_MEDIA_DEFS_H
