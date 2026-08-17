/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_MEDIA_FILE_H
#define _MEDIA2_MEDIA_FILE_H


#include <Entry.h>
#include <Url.h>

#include <media2/MediaFormat.h>


class BMediaTrack;


class BMediaFile {
public:
								BMediaFile(const entry_ref* ref);
								BMediaFile(const entry_ref* ref, int32 flags);
								BMediaFile(const BUrl& url);
								BMediaFile(const entry_ref* outputRef,
									const media_file_format* fileFormat,
									int32 flags = 0);
	virtual						~BMediaFile();

			status_t			InitCheck() const;

			int32				CountTracks() const;
			BMediaTrack*		TrackAt(int32 index);
			status_t			ReleaseTrack(BMediaTrack* track);

			BMediaTrack*		CreateTrack(const BMediaFormat& outputFormat);

			BMediaTrack*		CreateTrack(media_format* mf,
									const media_codec_info* codecInfo);

			status_t			CommitHeader();
			status_t			CloseFile();

private:
			class Impl;
			Impl*				fImpl;
};


#endif
