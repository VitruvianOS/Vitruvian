/*
 * Copyright 2017, Dario Casalinuovo. All rights reserved.
 * Copyright 2005, Marcus Overhagen, marcus@overhagen.de. All rights reserved.
 * Copyright 2005, Jérôme Duval. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "MediaPlay.h"

#include <Entry.h>
#include <media2/MediaFile.h>
#include <media2/MediaTrack.h>
#include <OS.h>
#include <media2/SoundPlayer.h>
#include <Url.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

sem_id finished = -1;
BMediaTrack* playTrack;
BMediaFormat playFormat;
BSoundPlayer* player = 0;
volatile bool interrupt = false;


void
play_buffer(void *cookie, void * buffer, size_t size, const media_raw_audio_format & format)
{




	const uint32 stride = (format.format & media_raw_audio_format::B_AUDIO_SIZE_MASK)
		* format.channel_count;
	int64 frames = stride > 0 ? (int64)(size / stride) : 0;


	if (frames <= 0 || playTrack->ReadFrames(buffer, &frames) != B_OK)
		frames = 0;



	const size_t bytesFilled = (size_t)frames * stride;
	if (bytesFilled < size)
		memset((uint8*)buffer + bytesFilled, 0, size - bytesFilled);

	if (frames <= 0) {
		player->SetHasData(false);
		release_sem(finished);
	}
}


void
keyb_int(int)
{

	interrupt = true;
	release_sem(finished);
}


int media_play(const char* uri)
{
	BUrl url;
	entry_ref ref;
	BMediaFile* playFile;

	if (get_ref_for_path(uri, &ref) != B_OK) {
		url.SetUrlString(uri, true);
		if (url.IsValid()) {
			playFile = new BMediaFile(url);
		} else
			return 2;
	} else
		playFile = new BMediaFile(&ref);

	if (playFile->InitCheck() != B_OK) {
		delete playFile;
		return 2;
	}

	for (int i = 0; i < playFile->CountTracks(); i++) {
		BMediaTrack* track = playFile->TrackAt(i);
		if (track != NULL) {
			playFormat.format.type = B_MEDIA_RAW_AUDIO;
			if ((track->DecodedFormat(&playFormat) == B_OK)
				&& playFormat.IsRawAudio()) {
				playTrack = track;
				break;
			}
			playFile->ReleaseTrack(track);
		}
	}

	if (playTrack == NULL) {
		fprintf(stderr, "media_play: no playable audio track in \"%s\"\n", uri);
		delete playFile;
		return 2;
	}


	signal(SIGINT, keyb_int);

	finished = create_sem(0, "finish wait");

	printf("Playing file...\n");


	player = new BSoundPlayer(&playFormat, "playfile", play_buffer);
	if (player->InitCheck() != B_OK) {
		fprintf(stderr, "media_play: couldn't create sound player: %s\n",
			strerror(player->InitCheck()));
		delete player;
		delete_sem(finished);
		delete playFile;
		return 2;
	}
	player->SetVolume(1.0f);


	player->SetHasData(true);
	status_t err = player->Start();
	if (err != B_OK) {
		fprintf(stderr, "media_play: couldn't start playback: %s\n",
			strerror(err));
		delete player;
		delete_sem(finished);
		delete playFile;
		return 2;
	}

	acquire_sem(finished);

	if (interrupt == true) {




		printf("Interrupted\n");
		player->Stop();
	}

	printf("Playback finished.\n");

	delete player;
	delete playFile;

	return 0;
}
