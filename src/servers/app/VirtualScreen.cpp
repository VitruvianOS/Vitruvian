/*
 * Copyright 2005-2013, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include "VirtualScreen.h"

#include "HWInterface.h"
#include "Desktop.h"

#include <new>


VirtualScreen::VirtualScreen()
	:
	fScreenList(4),
	fDrawingEngine(NULL),
	fHWInterface(NULL)
{
}


VirtualScreen::~VirtualScreen()
{
	_Reset();
}


void
VirtualScreen::_Reset()
{
	ScreenList list;
	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);

		list.AddItem(item->screen);
	}

	gScreenManager->ReleaseScreens(list);
	fScreenList.MakeEmpty();

	fFrame.Set(0, 0, 0, 0);
	fDrawingEngine = NULL;
	fHWInterface = NULL;
}


status_t
VirtualScreen::SetConfiguration(Desktop& desktop, const ScreenConfigurations& configurations,
	ScreenConfigurations& currentConfigurations, uint32* _changedScreens)
{
	// Remember previous screen modes

	typedef std::map<Screen*, display_mode> ScreenModeMap;
	ScreenModeMap previousModes;
	bool previousModesFailed = false;

	if (_changedScreens != NULL) {
		*_changedScreens = 0;

		try {
			for (int32 i = 0; i < fScreenList.CountItems(); i++) {
				Screen* screen = fScreenList.ItemAt(i)->screen;

				display_mode mode;
				screen->GetMode(mode);

				previousModes.insert(std::make_pair(screen, mode));
			}
		} catch (...) {
			previousModesFailed = true;
			*_changedScreens = ~0L;
		}
	}

	_Reset();

	ScreenList list;
	status_t status = gScreenManager->AcquireScreens(&desktop, NULL, 0,
		desktop.TargetScreen(), false, list);
	if (status != B_OK) {
		// TODO: we would try again here with force == true
		return status;
	}

	for (int32 i = 0; i < list.CountItems(); i++) {
		Screen* screen = list.ItemAt(i);

		AddScreen(screen, configurations, currentConfigurations);

		if (!previousModesFailed && _changedScreens != NULL) {
			// Figure out which screens have changed their mode
			display_mode mode;
			screen->GetMode(mode);

			ScreenModeMap::const_iterator found = previousModes.find(screen);
			if (found != previousModes.end()
				&& memcmp(&mode, &found->second, sizeof(display_mode)))
				*_changedScreens |= 1 << i;
		}
	}

	UpdateFrame();
	return B_OK;
}


status_t
VirtualScreen::AddScreen(Screen* screen, const ScreenConfigurations& configurations,
	ScreenConfigurations& currentConfigurations)
{
	screen_item* item = new(std::nothrow) screen_item;
	if (item == NULL)
		return B_NO_MEMORY;

	item->screen = screen;

	status_t status = B_ERROR;
	display_mode mode;
	if (_GetMode(screen, configurations, mode) == B_OK) {
		// we found settings for this screen, and try to apply them now
		status = screen->SetMode(mode);
	}

	if (status != B_OK) {
		// We found no configuration or it wasn't valid, try to fallback to
		// sane values
		status = screen->SetPreferredMode();
		if (status == B_OK) {
			screen->GetMode(mode);
		}
		if (status != B_OK)
			status = screen->SetBestMode(1024, 768, B_RGB32, 60.f);
		if (status != B_OK)
			status = screen->SetBestMode(800, 600, B_RGB32, 60.f, false);
		if (status != B_OK) {
			debug_printf("app_server: Failed to set mode: %s\n",
				strerror(status));

			delete item;
			return status;
		}
	}

	monitor_info info;
	bool hasInfo = screen->GetMonitorInfo(info) == B_OK;

	// Another screen goes back where it was, or right of the others.
	if (fScreenList.CountItems() > 0) {
		screen_configuration* stored = configurations.BestFit(screen->ID(),
			hasInfo ? &info : NULL);
		if (stored != NULL && stored->id == screen->ID()
			&& stored->frame.left >= 0 && stored->frame.top >= 0)
			screen->HWInterface()->SetOrigin(stored->frame.LeftTop());
		else
			screen->HWInterface()->SetOrigin(BPoint(fFrame.right + 1, 0));
	}

	currentConfigurations.Set(screen->ID(), hasInfo ? &info : NULL, screen->Frame(), mode);

	// All drawing goes through the first screen's interface, which
	// presents every screen's part.
	if (fScreenList.CountItems() == 0) {
		fDrawingEngine = screen->GetDrawingEngine();
		fHWInterface = screen->HWInterface();
	}
	item->frame = screen->Frame();

	fScreenList.AddItem(item);
	UpdateFrame();

	return B_OK;
}


status_t
VirtualScreen::RemoveScreen(Screen* screen)
{
	// The first screen is the one everything is drawn through.
	const int32 index = ScreenIndex(screen);
	if (index <= 0)
		return B_BAD_VALUE;

	delete fScreenList.RemoveItemAt(index);
		// the item, not the screen
	UpdateFrame();
	return B_OK;
}


void
VirtualScreen::UpdateFrame()
{
	fFrame.Set(0, 0, 0, 0);

	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		const BRect frame = fScreenList.ItemAt(i)->screen->Frame();
		fFrame = i == 0 ? frame : fFrame | frame;
	}
}


BRegion
VirtualScreen::Region() const
{
	BRegion region;
	for (int32 i = 0; i < fScreenList.CountItems(); i++)
		region.Include(fScreenList.ItemAt(i)->screen->Frame());
	return region;
}


void
VirtualScreen::Normalize()
{
	if (fScreenList.CountItems() == 0)
		return;

	// A screen that grew into a later one pushes it to the right. Two
	// screens at the same place mirror each other and stay.
	for (int32 i = 1; i < fScreenList.CountItems(); i++) {
		Screen* screen = fScreenList.ItemAt(i)->screen;
		for (int32 j = 0; j < i; j++) {
			const BRect frame = screen->Frame();
			const BRect other = fScreenList.ItemAt(j)->screen->Frame();
			if (frame.LeftTop() != other.LeftTop()
				&& frame.Intersects(other)) {
				screen->HWInterface()->SetOrigin(
					BPoint(other.right + 1, frame.top));
			}
		}
	}

	// A screen that shrank leaves a gap to the one on its right, which
	// then moves up to it.
	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		Screen* screen = fScreenList.ItemAt(i)->screen;
		const BRect frame = screen->Frame();

		float edge = -1;
		for (int32 j = 0; j < fScreenList.CountItems(); j++) {
			const BRect other = fScreenList.ItemAt(j)->screen->Frame();
			if (j == i || other.bottom < frame.top
				|| other.top > frame.bottom)
				continue;
			if (other.right >= frame.left) {
				// touching or overlapping: nothing to close
				if (other.left < frame.left) {
					edge = -1;
					break;
				}
				continue;
			}
			edge = max_c(edge, other.right);
		}

		if (edge >= 0 && frame.left > edge + 1)
			screen->HWInterface()->SetOrigin(BPoint(edge + 1, frame.top));
	}

	BPoint offset = fScreenList.ItemAt(0)->screen->Frame().LeftTop();
	for (int32 i = 1; i < fScreenList.CountItems(); i++) {
		const BRect frame = fScreenList.ItemAt(i)->screen->Frame();
		offset.x = min_c(offset.x, frame.left);
		offset.y = min_c(offset.y, frame.top);
	}

	if (offset != B_ORIGIN) {
		for (int32 i = 0; i < fScreenList.CountItems(); i++) {
			Screen* screen = fScreenList.ItemAt(i)->screen;
			screen->HWInterface()->SetOrigin(
				screen->Frame().LeftTop() - offset);
		}
	}

	UpdateFrame();
}


/*!	Returns the smallest frame that spans over all screens
*/
BRect
VirtualScreen::Frame() const
{
	return fFrame;
}


Screen*
VirtualScreen::ScreenAt(int32 index) const
{
	screen_item* item = fScreenList.ItemAt(index);
	if (item != NULL)
		return item->screen;

	return NULL;
}


Screen*
VirtualScreen::ScreenByID(int32 id) const
{
	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);

		if (item->screen->ID() == id)
			return item->screen;
	}

	// The main screen is the first one.
	if (id == B_MAIN_SCREEN_ID.id)
		return ScreenAt(0);

	return NULL;
}


int32
VirtualScreen::ScreenIndex(const Screen* screen) const
{
	for (int32 i = fScreenList.CountItems(); i-- > 0;) {
		if (fScreenList.ItemAt(i)->screen == screen)
			return i;
	}

	return -1;
}


void
VirtualScreen::SetScreenFrame(int32 index, BRect frame)
{
	screen_item* item = fScreenList.ItemAt(index);
	if (item != NULL)
		item->frame = frame;
}


BRect
VirtualScreen::ScreenFrameAt(int32 index) const
{
	screen_item* item = fScreenList.ItemAt(index);
	if (item != NULL)
		return item->frame;

	return BRect(0, 0, 0, 0);
}


int32
VirtualScreen::CountScreens() const
{
	return fScreenList.CountItems();
}


status_t
VirtualScreen::_GetMode(Screen* screen, const ScreenConfigurations& configurations,
	display_mode& mode) const
{
	monitor_info info;
	bool hasInfo = screen->GetMonitorInfo(info) == B_OK;

	screen_configuration* configuration = configurations.BestFit(screen->ID(),
		hasInfo ? &info : NULL);
	if (configuration == NULL)
		return B_NAME_NOT_FOUND;

	mode = configuration->mode;
	return B_OK;
}
