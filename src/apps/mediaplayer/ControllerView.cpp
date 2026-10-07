/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "ControllerView.h"

#include <Autolock.h>
#include <Message.h>

#include "Controller.h"
#include "Playlist.h"


ControllerView::ControllerView(BRect frame, Controller* controller,
		Playlist* playlist)
	:
	TransportControlGroup(frame, true, true, false),
	fController(controller),
	fPlaylist(playlist)
{
}


ControllerView::~ControllerView()
{
}


void
ControllerView::TogglePlaying()
{
	BAutolock _(fPlaylist);
	if (fPlaylist->CurrentItemIndex() == fPlaylist->CountItems() - 1
			&& Position() >= 0.999f) {
		// End of last item: wrap like the legacy player.
		fPlaylist->SetCurrentItemIndex(0, true);
	} else
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


void
ControllerView::Forward()
{
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


void
ControllerView::PositionChanged(float value)
{
	(void)value;
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
