/*
 * Copyright 2025-2026, Dario Casalinuovo. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/MediaFormat.h>

#include <string.h>

#include <ByteOrder.h>


static void
fill_default_positions(uint32 count, media_channel_position* out)
{
	for (uint32 i = 0; i < B_AUDIO_MAX_CHANNELS; i++)
		out[i] = B_CHANNEL_NONE;

	switch (count) {
		case 1:
			out[0] = B_CHANNEL_CENTER;
			break;
		case 2:
			out[0] = B_CHANNEL_FRONT_LEFT;
			out[1] = B_CHANNEL_FRONT_RIGHT;
			break;
		case 4:
			out[0] = B_CHANNEL_FRONT_LEFT;
			out[1] = B_CHANNEL_FRONT_RIGHT;
			out[2] = B_CHANNEL_REAR_LEFT;
			out[3] = B_CHANNEL_REAR_RIGHT;
			break;
		case 6:
			out[0] = B_CHANNEL_FRONT_LEFT;
			out[1] = B_CHANNEL_FRONT_RIGHT;
			out[2] = B_CHANNEL_CENTER;
			out[3] = B_CHANNEL_LFE;
			out[4] = B_CHANNEL_REAR_LEFT;
			out[5] = B_CHANNEL_REAR_RIGHT;
			break;
		case 8:
			out[0] = B_CHANNEL_FRONT_LEFT;
			out[1] = B_CHANNEL_FRONT_RIGHT;
			out[2] = B_CHANNEL_CENTER;
			out[3] = B_CHANNEL_LFE;
			out[4] = B_CHANNEL_REAR_LEFT;
			out[5] = B_CHANNEL_REAR_RIGHT;
			out[6] = B_CHANNEL_SIDE_LEFT;
			out[7] = B_CHANNEL_SIDE_RIGHT;
			break;
		default:
			for (uint32 i = 0; i < count && i < B_AUDIO_MAX_CHANNELS; i++)
				out[i] = B_CHANNEL_UNKNOWN;
			break;
	}
}


BMediaFormat::BMediaFormat()
	:
	format(),
	is_planar(false)
{
	format.type = B_MEDIA_NO_TYPE;
	for (uint32 i = 0; i < B_AUDIO_MAX_CHANNELS; i++)
		channel_positions[i] = B_CHANNEL_NONE;
}


BMediaFormat::BMediaFormat(const media_raw_audio_format& raw)
	:
	format(),
	is_planar(false)
{
	format.type = B_MEDIA_RAW_AUDIO;
	format.u.raw_audio = raw;
	fill_default_positions(raw.channel_count, channel_positions);
}


BMediaFormat::BMediaFormat(const media_raw_video_format& raw)
	:
	format(),
	is_planar(false)
{
	format.type = B_MEDIA_RAW_VIDEO;
	format.u.raw_video = raw;
	for (uint32 i = 0; i < B_AUDIO_MAX_CHANNELS; i++)
		channel_positions[i] = B_CHANNEL_NONE;
}


BMediaFormat::BMediaFormat(const media_encoded_audio_format& enc)
	:
	format(),
	is_planar(false)
{
	format.type = B_MEDIA_ENCODED_AUDIO;
	format.u.encoded_audio = enc;
	for (uint32 i = 0; i < B_AUDIO_MAX_CHANNELS; i++)
		channel_positions[i] = B_CHANNEL_NONE;
}


BMediaFormat::BMediaFormat(const media_encoded_video_format& enc)
	:
	format(),
	is_planar(false)
{
	format.type = B_MEDIA_ENCODED_VIDEO;
	format.u.encoded_video = enc;
	for (uint32 i = 0; i < B_AUDIO_MAX_CHANNELS; i++)
		channel_positions[i] = B_CHANNEL_NONE;
}


void
BMediaFormat::SetToDefault()
{
	format = media_format();
	format.type = B_MEDIA_RAW_AUDIO;
	media_raw_audio_format& raw = format.u.raw_audio;
	raw.frame_rate    = 48000.0f;
	raw.channel_count = 2;
	raw.format        = media_raw_audio_format::B_AUDIO_FLOAT;
	raw.byte_order    = B_MEDIA_LITTLE_ENDIAN;
	raw.buffer_size   = 2048;
	is_planar         = false;
	fill_default_positions(2, channel_positions);
}


bool
BMediaFormat::Matches(const BMediaFormat& other) const
{
	if (format.type != other.format.type)
		return false;
	if (format.type == B_MEDIA_RAW_AUDIO) {
		const media_raw_audio_format& a = format.u.raw_audio;
		const media_raw_audio_format& b = other.format.u.raw_audio;
		if (a.frame_rate    != b.frame_rate)    return false;
		if (a.channel_count != b.channel_count) return false;
		if (a.format        != b.format)        return false;
		if (a.byte_order    != b.byte_order)    return false;
		if (is_planar       != other.is_planar) return false;
		for (uint32 i = 0; i < a.channel_count && i < B_AUDIO_MAX_CHANNELS; i++) {
			if (channel_positions[i] != other.channel_positions[i])
				return false;
		}
		return true;
	}
	// Encoded / video: byte-compare the union for now.
	return memcmp(&format.u, &other.format.u, sizeof(format.u)) == 0;
}


// #pragma mark - BMediaFormatBuilder


BMediaFormatBuilder::BMediaFormatBuilder(media_type type)
{
	if (type == B_MEDIA_RAW_AUDIO)
		fFormat.SetToDefault();
	else
		fFormat.format.type = type;
}


BMediaFormatBuilder::BMediaFormatBuilder(const BMediaFormat& base)
	:
	fFormat(base)
{
}


BMediaFormatBuilder&
BMediaFormatBuilder::SetFrameRate(float rate)
{
	fFormat.format.u.raw_audio.frame_rate = rate;
	return *this;
}


BMediaFormatBuilder&
BMediaFormatBuilder::SetChannelCount(uint32 count)
{
	fFormat.format.u.raw_audio.channel_count = count;
	return *this;
}


BMediaFormatBuilder&
BMediaFormatBuilder::SetSampleFormat(uint32 haikuFmt)
{
	fFormat.format.u.raw_audio.format = haikuFmt;
	return *this;
}


BMediaFormatBuilder&
BMediaFormatBuilder::SetByteOrder(uint32 byteOrder)
{
	fFormat.format.u.raw_audio.byte_order = byteOrder;
	return *this;
}


BMediaFormatBuilder&
BMediaFormatBuilder::SetBufferSize(size_t bytes)
{
	fFormat.format.u.raw_audio.buffer_size = bytes;
	return *this;
}


BMediaFormatBuilder&
BMediaFormatBuilder::SetPlanar(bool planar)
{
	fFormat.is_planar = planar;
	return *this;
}


BMediaFormatBuilder&
BMediaFormatBuilder::SetChannelPositions(const media_channel_position* pos,
	uint32 count)
{
	uint32 limit = count < B_AUDIO_MAX_CHANNELS ? count : B_AUDIO_MAX_CHANNELS;
	for (uint32 i = 0; i < limit; i++)
		fFormat.channel_positions[i] = pos[i];
	for (uint32 i = limit; i < B_AUDIO_MAX_CHANNELS; i++)
		fFormat.channel_positions[i] = B_CHANNEL_NONE;
	return *this;
}


BMediaFormatBuilder&
BMediaFormatBuilder::SetDefaultChannelPositions()
{
	fill_default_positions(fFormat.format.u.raw_audio.channel_count,
		fFormat.channel_positions);
	return *this;
}


BMediaFormat
BMediaFormatBuilder::End() const
{
	return fFormat;
}
