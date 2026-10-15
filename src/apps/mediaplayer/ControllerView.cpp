/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "ControllerView.h"

#include <Autolock.h>
#include <Message.h>
#include <OS.h>

#include "Controller.h"
#include "Playlist.h"


ControllerView::ControllerView(BRect frame, Controller* controller,
		Playlist* playlist)
	:
	TransportControlGroup(frame, true, true, true),
	fController(controller),
	fPlaylist(playlist),
	fLastScrubTime(0)
{
}


ControllerView::~ControllerView()
{
}


void
ControllerView::TogglePlaying()
{
	fController->TogglePlaying();
}


void
ControllerView::Stop()
{
	fController->Stop();
}


void
ControllerView::Rewind()
{
	fController->Rewind();
}


// When paused on video the forward control steps a frame instead of
// jumping five seconds, matching the legacy player's step behavior.
void
ControllerView::Forward()
{
	if (fController->State() == BMediaPlayer::B_PLAYER_PAUSED
			&& fController->HasVideo()) {
		fController->StepFrame(true);
		return;
	}
	fController->Forward();
}


void
ControllerView::SkipBackward()
{
	fController->SkipBackward();
}


void
ControllerView::SkipForward()
{
	fController->SkipForward();
}


void
ControllerView::VolumeChanged(float value)
{
	fController->SetVolume(value);
}


void
ControllerView::ToggleMute()
{
	fController->SetMute(!fController->IsMuted());
	SetMuted(fController->IsMuted());
}


// Scrubbing: a throttled key-unit seek per drag step so the preview
// frame keeps up without flooding the pipeline; the accurate seek lands
// on release in PositionCommitted.
void
ControllerView::PositionChanged(float value)
{
	bigtime_t now = system_time();
	if (now - fLastScrubTime < 100000)
		return;
	fLastScrubTime = now;
	fController->SeekTo(TimePositionFor(value), false);
}


void
ControllerView::PositionCommitted(float value)
{
	fLastScrubTime = 0;
	fController->SeekTo(TimePositionFor(value), true);
}


bigtime_t
ControllerView::TimePositionFor(float value)
{
	bigtime_t duration = fController->Duration();
	if (duration <= 0)
		return 0;
	bigtime_t position = (bigtime_t)(value * duration);
	if (position > duration)
		position = duration;
	return position;
}


void
ControllerView::_CheckSkippable()
{
	int32 count = fPlaylist->CountItems();
	int32 index = fPlaylist->CurrentItemIndex();
	SetSkippable(index > 0, index < count - 1);
}
