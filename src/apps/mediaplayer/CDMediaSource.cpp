/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "CDMediaSource.h"

#include <String.h>

#include <storage/Volume.h>
#include <storage/VolumeRoster.h>

#include <media2/MediaFile.h>
#include <media2/MediaTrack.h>


static const char* kCdDevices[] = {
	"/dev/sr0",
	"/dev/sr1",
	"/dev/cdrom",
	NULL
};


CDMediaSource::CDMediaSource()
{
}


CDMediaSource::~CDMediaSource()
{
	_Clear();
}


void
CDMediaSource::_Clear()
{
	for (int32 i = 0; i < fTracks.CountItems(); i++)
		delete (BUrl*)fTracks.ItemAt(i);
	fTracks.MakeEmpty();
	fDevice = "";
}


status_t
CDMediaSource::BuildTrackUrl(const char* device, int32 track, BUrl* url)
{
	if (device == NULL || url == NULL || track < 1)
		return B_BAD_VALUE;
	BString uri("cdda://");
	uri << device << "/" << track;
	url->SetUrlString(uri, false);
	return B_OK;
}


status_t
CDMediaSource::_AppendTrack(int32 track)
{
	BUrl url;
	status_t err = BuildTrackUrl(fDevice.String(), track, &url);
	if (err != B_OK)
		return err;

	// Confirm the URI is openable through BMediaFile (playbin + cdda).
	// A failure means the GStreamer cdda path is not available here.
	BMediaFile file(url);
	if (file.InitCheck() != B_OK)
		return file.InitCheck();

	BUrl* stored = new BUrl(url);
	if (stored == NULL)
		return B_NO_MEMORY;
	if (fTracks.AddItem(stored) == false) {
		delete stored;
		return B_NO_MEMORY;
	}
	return B_OK;
}


status_t
CDMediaSource::_TryDevice(const char* device)
{
	fDevice = device;

	// The playbin path opens a single raw-audio track per URI, so probe
	// track 1 and then a bounded scan for any additional tracks that open.
	int32 found = 0;
	for (int32 track = 1; track <= 99; track++) {
		status_t err = _AppendTrack(track);
		if (err == B_OK) {
			found++;
			continue;
		}
		// Track 1 is the only one that must open for a usable CD source.
		if (track == 1)
			return err;
		break;
	}
	return found > 0 ? B_OK : B_ERROR;
}


status_t
CDMediaSource::Probe()
{
	_Clear();

	// Volumes first: a mounted audio CD shows a DeviceNode (e.g. /dev/sr0).
	BVolumeRoster roster;
	BVolume volume;
	while (roster.GetNextVolume(&volume) == B_OK) {
		BString node;
		if (volume.DeviceNode(&node) != B_OK || node.Length() == 0)
			continue;
		if (node.FindFirst("sr") < 0 && node.FindFirst("cdrom") < 0)
			continue;
		if (_TryDevice(node.String()) == B_OK)
			return B_OK;
		_Clear();
	}

	// Fall back to well-known CD device nodes.
	for (int32 i = 0; kCdDevices[i] != NULL; i++) {
		if (_TryDevice(kCdDevices[i]) == B_OK)
			return B_OK;
		_Clear();
	}
	return B_ENTRY_NOT_FOUND;
}


status_t
CDMediaSource::TrackUrl(int32 track, BUrl* url) const
{
	if (track < 1 || track > fTracks.CountItems() || url == NULL)
		return B_BAD_VALUE;
	*url = *(BUrl*)fTracks.ItemAt(track - 1);
	return B_OK;
}
