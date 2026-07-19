/*
 * Copyright 2025-2026, Dario Casalinuovo. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include "FormatConversion.h"

#include <math.h>
#include <string.h>

#include <ByteOrder.h>


namespace BPrivate { namespace media {


spa_audio_format
NativeSampleFormatToSPA(uint32 nativeFmt, uint32 byteOrder)
{
	const bool isLE = byteOrder == B_MEDIA_LITTLE_ENDIAN;
	switch (nativeFmt) {
		case media_raw_audio_format::B_AUDIO_FLOAT:
			return isLE ? SPA_AUDIO_FORMAT_F32_LE : SPA_AUDIO_FORMAT_F32_BE;
		case media_raw_audio_format::B_AUDIO_DOUBLE:
			return isLE ? SPA_AUDIO_FORMAT_F64_LE : SPA_AUDIO_FORMAT_F64_BE;
		case media_raw_audio_format::B_AUDIO_SHORT:
			return isLE ? SPA_AUDIO_FORMAT_S16_LE : SPA_AUDIO_FORMAT_S16_BE;
		case media_raw_audio_format::B_AUDIO_INT:
			return isLE ? SPA_AUDIO_FORMAT_S32_LE : SPA_AUDIO_FORMAT_S32_BE;
		case media_raw_audio_format::B_AUDIO_UCHAR:
			return SPA_AUDIO_FORMAT_U8;
		case media_raw_audio_format::B_AUDIO_CHAR:
			return SPA_AUDIO_FORMAT_S8;
		default:
			return SPA_AUDIO_FORMAT_UNKNOWN;
	}
}


void
SPASampleFormatToNative(spa_audio_format spaFmt, uint32* nativeFmt,
	uint32* byteOrder)
{
	switch (spaFmt) {
		case SPA_AUDIO_FORMAT_F32_LE:
			*nativeFmt = media_raw_audio_format::B_AUDIO_FLOAT;
			*byteOrder = B_MEDIA_LITTLE_ENDIAN;
			return;
		case SPA_AUDIO_FORMAT_F32_BE:
			*nativeFmt = media_raw_audio_format::B_AUDIO_FLOAT;
			*byteOrder = B_MEDIA_BIG_ENDIAN;
			return;
		case SPA_AUDIO_FORMAT_F64_LE:
			*nativeFmt = media_raw_audio_format::B_AUDIO_DOUBLE;
			*byteOrder = B_MEDIA_LITTLE_ENDIAN;
			return;
		case SPA_AUDIO_FORMAT_F64_BE:
			*nativeFmt = media_raw_audio_format::B_AUDIO_DOUBLE;
			*byteOrder = B_MEDIA_BIG_ENDIAN;
			return;
		case SPA_AUDIO_FORMAT_S16_LE:
			*nativeFmt = media_raw_audio_format::B_AUDIO_SHORT;
			*byteOrder = B_MEDIA_LITTLE_ENDIAN;
			return;
		case SPA_AUDIO_FORMAT_S16_BE:
			*nativeFmt = media_raw_audio_format::B_AUDIO_SHORT;
			*byteOrder = B_MEDIA_BIG_ENDIAN;
			return;
		case SPA_AUDIO_FORMAT_S32_LE:
			*nativeFmt = media_raw_audio_format::B_AUDIO_INT;
			*byteOrder = B_MEDIA_LITTLE_ENDIAN;
			return;
		case SPA_AUDIO_FORMAT_S32_BE:
			*nativeFmt = media_raw_audio_format::B_AUDIO_INT;
			*byteOrder = B_MEDIA_BIG_ENDIAN;
			return;
		case SPA_AUDIO_FORMAT_U8:
			*nativeFmt = media_raw_audio_format::B_AUDIO_UCHAR;
			*byteOrder = 0;
			return;
		case SPA_AUDIO_FORMAT_S8:
			*nativeFmt = media_raw_audio_format::B_AUDIO_CHAR;
			*byteOrder = 0;
			return;
		default:
			*nativeFmt = 0;
			*byteOrder = 0;
			return;
	}
}


spa_audio_channel
NativeChannelToSPA(media_channel_position pos)
{
	switch (pos) {
		case B_CHANNEL_FRONT_LEFT:             return SPA_AUDIO_CHANNEL_FL;
		case B_CHANNEL_FRONT_RIGHT:            return SPA_AUDIO_CHANNEL_FR;
		case B_CHANNEL_CENTER:                 return SPA_AUDIO_CHANNEL_FC;
		case B_CHANNEL_LFE:                    return SPA_AUDIO_CHANNEL_LFE;
		case B_CHANNEL_REAR_LEFT:              return SPA_AUDIO_CHANNEL_RL;
		case B_CHANNEL_REAR_RIGHT:             return SPA_AUDIO_CHANNEL_RR;
		case B_CHANNEL_FRONT_LEFT_OF_CENTER:   return SPA_AUDIO_CHANNEL_FLC;
		case B_CHANNEL_FRONT_RIGHT_OF_CENTER:  return SPA_AUDIO_CHANNEL_FRC;
		case B_CHANNEL_REAR_CENTER:            return SPA_AUDIO_CHANNEL_RC;
		case B_CHANNEL_SIDE_LEFT:              return SPA_AUDIO_CHANNEL_SL;
		case B_CHANNEL_SIDE_RIGHT:             return SPA_AUDIO_CHANNEL_SR;
		case B_CHANNEL_NONE:                   return SPA_AUDIO_CHANNEL_NA;
		case B_CHANNEL_UNKNOWN:
		default:                                return SPA_AUDIO_CHANNEL_UNKNOWN;
	}
}


media_channel_position
SPAChannelToNative(spa_audio_channel ch)
{
	switch (ch) {
		case SPA_AUDIO_CHANNEL_FL:   return B_CHANNEL_FRONT_LEFT;
		case SPA_AUDIO_CHANNEL_FR:   return B_CHANNEL_FRONT_RIGHT;
		case SPA_AUDIO_CHANNEL_FC:   return B_CHANNEL_CENTER;
		case SPA_AUDIO_CHANNEL_LFE:  return B_CHANNEL_LFE;
		case SPA_AUDIO_CHANNEL_RL:   return B_CHANNEL_REAR_LEFT;
		case SPA_AUDIO_CHANNEL_RR:   return B_CHANNEL_REAR_RIGHT;
		case SPA_AUDIO_CHANNEL_FLC:  return B_CHANNEL_FRONT_LEFT_OF_CENTER;
		case SPA_AUDIO_CHANNEL_FRC:  return B_CHANNEL_FRONT_RIGHT_OF_CENTER;
		case SPA_AUDIO_CHANNEL_RC:   return B_CHANNEL_REAR_CENTER;
		case SPA_AUDIO_CHANNEL_SL:   return B_CHANNEL_SIDE_LEFT;
		case SPA_AUDIO_CHANNEL_SR:   return B_CHANNEL_SIDE_RIGHT;
		case SPA_AUDIO_CHANNEL_NA:   return B_CHANNEL_NONE;
		default:                     return B_CHANNEL_UNKNOWN;
	}
}


bool
BuildSPAAudioInfo(const BMediaFormat& fmt, spa_audio_info_raw* out)
{
	if (!fmt.IsRawAudio())
		return false;

	const media_raw_audio_format& raw = fmt.format.u.raw_audio;
	memset(out, 0, sizeof(*out));
	out->format   = NativeSampleFormatToSPA(raw.format, raw.byte_order);
	out->rate     = (uint32)lroundf(raw.frame_rate);
	out->channels = raw.channel_count;
	out->flags    = fmt.is_planar ? SPA_AUDIO_FLAG_UNPOSITIONED : 0;

	for (uint32 i = 0; i < raw.channel_count && i < B_AUDIO_MAX_CHANNELS; i++)
		out->position[i] = NativeChannelToSPA(fmt.channel_positions[i]);

	return out->format != SPA_AUDIO_FORMAT_UNKNOWN;
}


void
ExtractFromSPAAudio(const spa_audio_info_raw& spa, BMediaFormat* out)
{
	out->format.type = B_MEDIA_RAW_AUDIO;
	media_raw_audio_format& raw = out->format.u.raw_audio;
	raw.frame_rate    = (float)spa.rate;
	raw.channel_count = spa.channels;
	raw.buffer_size   = 0;
	SPASampleFormatToNative(spa.format, &raw.format, &raw.byte_order);

	out->is_planar = (spa.flags & SPA_AUDIO_FLAG_UNPOSITIONED) != 0;

	for (uint32 i = 0; i < spa.channels && i < B_AUDIO_MAX_CHANNELS; i++) {
		out->channel_positions[i] = SPAChannelToNative(
			(spa_audio_channel)spa.position[i]);
	}
	for (uint32 i = spa.channels; i < B_AUDIO_MAX_CHANNELS; i++)
		out->channel_positions[i] = B_CHANNEL_NONE;
}


// #pragma mark - video


spa_video_format
NativeVideoFormatToSPA(uint32 colorSpace)
{
	switch (colorSpace) {
		case B_RGB32:    return SPA_VIDEO_FORMAT_BGRx;
		case B_RGBA32:   return SPA_VIDEO_FORMAT_BGRA;
		case B_YCbCr422: return SPA_VIDEO_FORMAT_YUY2;
		case B_YUV420:   return SPA_VIDEO_FORMAT_I420;
		default:         return SPA_VIDEO_FORMAT_UNKNOWN;
	}
}


uint32
SPAVideoFormatToNative(spa_video_format spaFmt)
{
	switch (spaFmt) {
		case SPA_VIDEO_FORMAT_BGRx:
		case SPA_VIDEO_FORMAT_BGRA: return B_RGB32;
		case SPA_VIDEO_FORMAT_RGBA: return B_RGBA32;
		case SPA_VIDEO_FORMAT_YUY2: return B_YCbCr422;
		case SPA_VIDEO_FORMAT_I420: return B_YUV420;
		default:                    return B_NO_COLOR_SPACE;
	}
}


bool
BuildSPAVideoInfo(const BMediaFormat& fmt, spa_video_info_raw* out)
{
	if (!fmt.IsRawVideo())
		return false;
	const media_raw_video_format& rv = fmt.format.u.raw_video;
	memset(out, 0, sizeof(*out));
	out->format        = NativeVideoFormatToSPA(rv.color_space);
	out->size.width    = rv.width;
	out->size.height   = rv.height;
	out->framerate.num = (uint32)(rv.field_rate);
	out->framerate.denom = 1;
	out->pixel_aspect_ratio.num = rv.pixel_width_aspect != 0
		? rv.pixel_width_aspect : 1;
	out->pixel_aspect_ratio.denom = rv.pixel_height_aspect != 0
		? rv.pixel_height_aspect : 1;
	return out->format != SPA_VIDEO_FORMAT_UNKNOWN;
}


void
ExtractFromSPAVideo(const spa_video_info_raw& spa, BMediaFormat* out)
{
	out->format.type = B_MEDIA_RAW_VIDEO;
	media_raw_video_format& rv = out->format.u.raw_video;
	rv.width       = spa.size.width;
	rv.height      = spa.size.height;
	rv.color_space = SPAVideoFormatToNative(spa.format);
	rv.field_rate  = spa.framerate.denom != 0
		? (float)spa.framerate.num / (float)spa.framerate.denom : 0.0f;
	rv.pixel_width_aspect  = (uint16)spa.pixel_aspect_ratio.num;
	rv.pixel_height_aspect = (uint16)spa.pixel_aspect_ratio.denom;
	rv.orientation = B_VIDEO_TOP_LEFT_RIGHT;
}


} } // namespace BPrivate::media
