/*
 * Copyright 2025-2026, Dario Casalinuovo. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/MediaDefs.h>


// Wildcards: zero-initialized; legacy semantics treat a zero-field
// audio/video format as "any".
const media_raw_audio_format media_raw_audio_format_wildcard = {};
const media_raw_video_format media_raw_video_format_wildcard = {};


const media_raw_audio_format& media_raw_audio_format::wildcard
	= media_raw_audio_format_wildcard;
const media_raw_video_format& media_raw_video_format::wildcard
	= media_raw_video_format_wildcard;


// Kind constants — strings, legacy DefaultMediaTheme uses strcmp.
extern const char* const B_GAIN                = "B_GAIN";
extern const char* const B_MASTER_GAIN         = "B_MASTER_GAIN";
extern const char* const B_BALANCE             = "B_BALANCE";
extern const char* const B_FREQUENCY           = "B_FREQUENCY";
extern const char* const B_GENERIC             = "B_GENERIC";
extern const char* const B_INPUT_MUX           = "B_INPUT_MUX";
extern const char* const B_ENABLE              = "B_ENABLE";
extern const char* const B_MUTE                = "B_MUTE";
extern const char* const B_LEVEL               = "B_LEVEL";
extern const char* const B_SHUTTLE_SPEED       = "B_SHUTTLE_SPEED";
extern const char* const B_CROSSFADE           = "B_CROSSFADE";
extern const char* const B_EQUALIZATION        = "B_EQUALIZATION";
extern const char* const B_COMPRESSION         = "B_COMPRESSION";
extern const char* const B_QUALITY             = "B_QUALITY";
extern const char* const B_BITRATE             = "B_BITRATE";
extern const char* const B_GOP_SIZE            = "B_GOP_SIZE";
extern const char* const B_RESOLUTION          = "B_RESOLUTION";
extern const char* const B_COLOR_SPACE         = "B_COLOR_SPACE";
extern const char* const B_FRAME_RATE          = "B_FRAME_RATE";
extern const char* const B_VIDEO_FORMAT        = "B_VIDEO_FORMAT";
extern const char* const B_WEB_PHYSICAL_INPUT  = "B_WEB_PHYSICAL_INPUT";
extern const char* const B_WEB_PHYSICAL_OUTPUT = "B_WEB_PHYSICAL_OUTPUT";
extern const char* const B_WEB_ADC_CONVERTER   = "B_WEB_ADC_CONVERTER";
extern const char* const B_WEB_DAC_CONVERTER   = "B_WEB_DAC_CONVERTER";
extern const char* const B_WEB_LOGICAL_INPUT   = "B_WEB_LOGICAL_INPUT";
extern const char* const B_WEB_LOGICAL_OUTPUT  = "B_WEB_LOGICAL_OUTPUT";
extern const char* const B_WEB_LOGICAL_BUS     = "B_WEB_LOGICAL_BUS";
extern const char* const B_WEB_BUFFER_INPUT    = "B_WEB_BUFFER_INPUT";
extern const char* const B_WEB_BUFFER_OUTPUT   = "B_WEB_BUFFER_OUTPUT";
extern const char* const B_SIMPLE_TRANSPORT    = "B_SIMPLE_TRANSPORT";
