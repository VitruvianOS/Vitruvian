/*
 * Copyright 2025-2026, Dario Casalinuovo. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _VITRUVIAN_MEDIA2_FORMAT_CONVERSION_H
#define _VITRUVIAN_MEDIA2_FORMAT_CONVERSION_H


#include <spa/param/audio/raw.h>
#include <spa/param/video/raw.h>

#include <media2/MediaFormat.h>


namespace BPrivate { namespace media {


spa_audio_format		NativeSampleFormatToSPA(uint32 nativeFmt, uint32 byteOrder);
void					SPASampleFormatToNative(spa_audio_format spaFmt,
							uint32* nativeFmt, uint32* byteOrder);

spa_audio_channel		NativeChannelToSPA(media_channel_position pos);
media_channel_position	SPAChannelToNative(spa_audio_channel ch);

bool					BuildSPAAudioInfo(const BMediaFormat& fmt,
							spa_audio_info_raw* out);
void					ExtractFromSPAAudio(const spa_audio_info_raw& spa,
							BMediaFormat* out);


spa_video_format		NativeVideoFormatToSPA(uint32 colorSpace);
uint32					SPAVideoFormatToNative(spa_video_format spaFmt);

bool					BuildSPAVideoInfo(const BMediaFormat& fmt,
							spa_video_info_raw* out);
void					ExtractFromSPAVideo(const spa_video_info_raw& spa,
							BMediaFormat* out);


} } // namespace BPrivate::media


#endif // _VITRUVIAN_MEDIA2_FORMAT_CONVERSION_H
