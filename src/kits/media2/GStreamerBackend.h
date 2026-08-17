/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _VITRUVIAN_MEDIA2_GSTREAMER_BACKEND_H
#define _VITRUVIAN_MEDIA2_GSTREAMER_BACKEND_H


#include <gst/gst.h>

#include <media2/MediaFormat.h>


namespace BPrivate { namespace media {


class GStreamerBackend {
public:
	static GStreamerBackend*	GetInstance();

	static bool					GstCapsToRawAudio(const GstCaps* caps,
									BMediaFormat* out);

	static bool					GstCapsToEncodedAudio(const GstCaps* caps,
									BMediaFormat* out);
	static bool					GstCapsToEncodedVideo(const GstCaps* caps,
									BMediaFormat* out);

	static GstCaps*				RawAudioToGstCaps(const BMediaFormat& fmt);

	static bool					GstCapsToRawVideo(const GstCaps* caps,
									BMediaFormat* out);
	static GstCaps*				RawVideoToGstCaps(const BMediaFormat& fmt);

	void						SetPreferHardwareCodecs(bool prefer);

	GstElement*					CreateDecodePipeline(const char* uri);

	GstElement*					CreateEncodePipeline(const char* path,
									const BMediaFormat& outputFormat);

private:
								GStreamerBackend();
								~GStreamerBackend();
								GStreamerBackend(const GStreamerBackend&) = delete;
			GStreamerBackend&	operator=(const GStreamerBackend&) = delete;

	static void					_GstAudioFormatStringToNative(const char* fmt,
									uint32* nativeFmt, uint32* byteOrder);
	static const char*			_NativeAudioFormatToGst(uint32 nativeFmt,
									uint32 byteOrder);
	static void					_DefaultChannelPositions(int count,
									media_channel_position* out);
	static void					_GstMaskToChannelPositions(guint64 mask,
									int count, media_channel_position* out);
	static guint64				_ChannelPositionsToGstMask(
									const media_channel_position* pos, int count);

			void				_BoostHwEncoderRanks();
			void				_RestoreEncoderRanks();
			void				_BoostHwDecoderRanks();
			void				_RestoreDecoderRanks();

			bool				fPreferHwCodecs;
};


} }


#endif
