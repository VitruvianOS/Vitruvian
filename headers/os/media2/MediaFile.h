/*
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_MEDIA_FILE_H
#define _MEDIA2_MEDIA_FILE_H


#include <Entry.h>

#include <media2/MediaFormat.h>


class BMediaTrack;


class BMediaFile {
public:
								BMediaFile(const entry_ref* ref);
								BMediaFile(const entry_ref* ref, int32 flags);
									// `flags` accepted for source compat;
									// no behavioural meaning in media2.
								BMediaFile(const entry_ref* outputRef,
									const media_file_format* fileFormat,
									int32 flags = 0);
									// Write mode: codec inferred from
									// fileFormat->mime_type / short_name.
									// Add a track via CreateTrack().
	virtual						~BMediaFile();

			status_t			InitCheck() const;

			int32				CountTracks() const;
			BMediaTrack*		TrackAt(int32 index);
			status_t			ReleaseTrack(BMediaTrack* track);

			BMediaTrack*		CreateTrack(const BMediaFormat& outputFormat);
									// Write-mode only.
			BMediaTrack*		CreateTrack(media_format* mf,
									const media_codec_info* codecInfo);
									// Legacy compat overload.
			status_t			CommitHeader();
									// Write-mode only — call after CreateTrack
									// and before any WriteFrames.
			status_t			CloseFile();
									// Write-mode only — call after the last
									// WriteFrames to flush + finalize the muxer.

private:
			class Impl;
			Impl*				fImpl;
};


#endif // _MEDIA2_MEDIA_FILE_H
