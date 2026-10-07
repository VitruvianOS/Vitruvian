/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _CD_MEDIA_SOURCE_H
#define _CD_MEDIA_SOURCE_H


#include <List.h>
#include <String.h>
#include <Url.h>


// Audio CD source for MediaPlayer. The tree has no CD driver and no TOC
// API; the only playback surface today is GStreamer cdda:// through
// BMediaFile (playbin accepts the URI). Track list comes from
// BMediaFile::CountTracks() once W12 enumerates demux tracks; until then
// we probe cdda:// devices and report what actually opens.
class CDMediaSource {
public:
								CDMediaSource();
								~CDMediaSource();

			status_t			Probe();

			int32				CountTracks() const
									{ return fTracks.CountItems(); }
			status_t			TrackUrl(int32 track, BUrl* url) const;
			const BString&		Device() const
									{ return fDevice; }

			static	status_t	BuildTrackUrl(const char* device, int32 track,
									BUrl* url);

private:
			void				_Clear();
			status_t			_TryDevice(const char* device);
			status_t			_AppendTrack(int32 track);

private:
			BList				fTracks;	// BUrl*
			BString				fDevice;
};


#endif // _CD_MEDIA_SOURCE_H
