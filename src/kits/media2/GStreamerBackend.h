/*
 * Copyright 2025-2026, Dario Casalinuovo. All Rights Reserved.
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

	// ── Raw audio junction conversion (decode side) ─────────────────────
	static bool					GstCapsToRawAudio(const GstCaps* caps,
									BMediaFormat* out);

	// ── Encoded informational conversion (decode side) ──────────────────
	static bool					GstCapsToEncodedAudio(const GstCaps* caps,
									BMediaFormat* out);
	static bool					GstCapsToEncodedVideo(const GstCaps* caps,
									BMediaFormat* out);

	// ── Raw audio junction conversion (encode side) ─────────────────────
	static GstCaps*				RawAudioToGstCaps(const BMediaFormat& fmt);
		// Caller must gst_caps_unref the result. Returns NULL on failure.

	// ── Raw video junction conversion ────────────────────────────────────
	static bool					GstCapsToRawVideo(const GstCaps* caps,
									BMediaFormat* out);
	static GstCaps*				RawVideoToGstCaps(const BMediaFormat& fmt);

	// ── Hardware codec preference ───────────────────────────────────────
	void						SetPreferHardwareCodecs(bool prefer);
		// Default: prefer hw. Env var VITRUVIAN_MEDIA_SW_ONLY=1 overrides.

	// ── Pipeline construction ───────────────────────────────────────────
	// Decode: filesrc location=<path> ! decodebin
	// The returned pipeline is in NULL state — caller transitions to PLAYING.
	GstElement*					CreateDecodePipeline(const char* uri);

	// Encode: appsrc ! encodebin ! filesink
	// outputFormat must be IsEncodedAudio() (or IsEncodedVideo() once video
	// encode lands). Returns NULL on failure.
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


} } // namespace BPrivate::media


#endif // _VITRUVIAN_MEDIA2_GSTREAMER_BACKEND_H
