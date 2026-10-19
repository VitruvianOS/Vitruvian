/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_MEDIA_TRACK_H
#define _MEDIA2_MEDIA_TRACK_H


#include <media2/MediaFormat.h>


class BView;


// SeekToTime/SeekToFrame flags. Default (no flags) is an accurate seek:
// key frame plus refinement so the track lands at the requested time.
enum media_seek_flags {
	B_MEDIA_SEEK_ACCURATE	= 1 << 0,
	B_MEDIA_SEEK_KEY_UNIT	= 1 << 1,
};


struct media_header {
	bigtime_t	start_time;
	uint32		size_used;
	uint32		flags;
	uint32		_reserved[5];

	union {
		media_audio_header			audio;
		media_video_header			video;
		media_encoded_audio_header	encoded_audio;
		media_encoded_video_header	encoded_video;
	} u;
};


// BMediaTrack is always owned by the BMediaFile that produced it
// (TrackAt() or CreateTrack()). BMediaFile::ReleaseTrack() validates
// ownership; it does not free the object. Deleting a track yourself
// while the file still holds it is undefined.
class BMediaTrack {
public:
	virtual						~BMediaTrack();

			status_t			InitCheck() const;

			status_t			DecodedFormat(BMediaFormat* format);
			status_t			DecodedFormat(media_format* format,
									uint32 flags = 0);

			status_t			EncodedFormat(BMediaFormat* format) const;
			status_t			EncodedFormat(media_format* format) const;

			int64				CountFrames() const;
			bigtime_t			Duration() const;

			int64				CurrentFrame() const;
			bigtime_t			CurrentTime() const;

			status_t			ReadFrames(void* buffer, int64* outFrameCount,
									media_header* mh = NULL);

			status_t			SeekToTime(bigtime_t* inOutTime, int32 flags = 0);
			status_t			SeekToFrame(int64* inOutFrame, int32 flags = 0);


			// Media Kit 1.0 shape: frameCount is a frame/sample count and
			// flags carries media_frame_flags. The writer
			// ignores flags; it is accepted for source compatibility.
			status_t			WriteFrames(const void* data, int64 frameCount,
									int32 flags = 0);

			// Swaps the raw chunk most recently handed to WriteFrames
			// while it is still pending (raw write tracks only).
			status_t			ReplaceFrames(const void* data,
									int64 frameCount, int32 flags = 0);


			status_t			GetCodecInfo(media_codec_info* info) const;
			status_t			SetQuality(float quality);
			status_t			Flush();
			BView*				GetParameterView();

private:
								BMediaTrack();
								BMediaTrack(const BMediaTrack&) = delete;
			BMediaTrack&		operator=(const BMediaTrack&) = delete;

			class Impl;
			Impl*				fImpl;

	friend class BMediaFile;
};


#endif
