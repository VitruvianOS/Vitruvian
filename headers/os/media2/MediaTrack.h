/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_MEDIA_TRACK_H
#define _MEDIA2_MEDIA_TRACK_H


#include <media2/MediaFormat.h>


class BView;


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


class BMediaTrack {
public:
	virtual						~BMediaTrack();

			status_t			InitCheck() const;

			status_t			DecodedFormat(BMediaFormat* format);
			status_t			EncodedFormat(BMediaFormat* format);

			int64				CountFrames() const;
			bigtime_t			Duration() const;

			int64				CurrentFrame() const;
			bigtime_t			CurrentTime() const;

			status_t			ReadFrames(void* buffer, int64* outFrameCount,
									media_header* mh = NULL);

			status_t			SeekToTime(bigtime_t* inOutTime, int32 flags = 0);
			status_t			SeekToFrame(int64* inOutFrame, int32 flags = 0);


			status_t			WriteFrames(const void* buffer, int64 frameCount,
									const media_header* mh = NULL);


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
