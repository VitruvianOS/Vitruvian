/*
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_MEDIA_FORMAT_H
#define _MEDIA2_MEDIA_FORMAT_H


#include <media2/MediaDefs.h>


#define B_AUDIO_MAX_CHANNELS 8


enum media_channel_position {
	B_CHANNEL_NONE                   = 0,
	B_CHANNEL_FRONT_LEFT             = 1,
	B_CHANNEL_FRONT_RIGHT            = 2,
	B_CHANNEL_CENTER                 = 3,
	B_CHANNEL_LFE                    = 4,
	B_CHANNEL_REAR_LEFT              = 5,
	B_CHANNEL_REAR_RIGHT             = 6,
	B_CHANNEL_FRONT_LEFT_OF_CENTER   = 7,
	B_CHANNEL_FRONT_RIGHT_OF_CENTER  = 8,
	B_CHANNEL_REAR_CENTER            = 9,
	B_CHANNEL_SIDE_LEFT              = 10,
	B_CHANNEL_SIDE_RIGHT             = 11,
	B_CHANNEL_UNKNOWN                = 0xFF
};


class BMediaFormat {
public:
								BMediaFormat();
	explicit					BMediaFormat(const media_raw_audio_format&);
	explicit					BMediaFormat(const media_raw_video_format&);
	explicit					BMediaFormat(const media_encoded_audio_format&);
	explicit					BMediaFormat(const media_encoded_video_format&);

			media_type			Type() const           { return format.type; }
			bool				IsRawAudio() const     { return format.type == B_MEDIA_RAW_AUDIO; }
			bool				IsRawVideo() const     { return format.type == B_MEDIA_RAW_VIDEO; }
			bool				IsEncodedAudio() const { return format.type == B_MEDIA_ENCODED_AUDIO; }
			bool				IsEncodedVideo() const { return format.type == B_MEDIA_ENCODED_VIDEO; }

			bool				Matches(const BMediaFormat& other) const;
			void				SetToDefault();

public:
			media_format		format;

			// Extensions valid when format.type == B_MEDIA_RAW_AUDIO
			media_channel_position	channel_positions[B_AUDIO_MAX_CHANNELS];
			bool					is_planar;
};


class BMediaFormatBuilder {
public:
	explicit					BMediaFormatBuilder(media_type type = B_MEDIA_RAW_AUDIO);
	explicit					BMediaFormatBuilder(const BMediaFormat& base);

			BMediaFormatBuilder&	SetFrameRate(float rate);
			BMediaFormatBuilder&	SetChannelCount(uint32 count);
			BMediaFormatBuilder&	SetSampleFormat(uint32 haikuFmt);
			BMediaFormatBuilder&	SetByteOrder(uint32 byteOrder);
			BMediaFormatBuilder&	SetBufferSize(size_t bytes);
			BMediaFormatBuilder&	SetPlanar(bool planar);
			BMediaFormatBuilder&	SetChannelPositions(
									const media_channel_position* pos, uint32 count);
			BMediaFormatBuilder&	SetDefaultChannelPositions();

			BMediaFormat		End() const;

private:
			BMediaFormat		fFormat;
};


#endif // _MEDIA2_MEDIA_FORMAT_H
