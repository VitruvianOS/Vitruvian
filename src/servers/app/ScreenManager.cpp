/*
 * Copyright 2005-2009, Haiku.
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *		Dario Casalinuovo
 */

/**	Manages all available physical screens */


#include "ScreenManager.h"

#include "Screen.h"
#include "ServerConfig.h"
#include "VirtualOutput.h"

#include <Autolock.h>
#include <Entry.h>
#include <NodeMonitor.h>

#include <algorithm>
#include <new>
#include <string.h>
#include <vector>

using std::nothrow;


#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
#ifndef __VOS__
#	include "AccelerantHWInterface.h"
#else
//#	include "SDLInterface.h"
#	include "DrmHWInterface.h"
#	include "FBDevHWInterface.h"
#endif
#else
 #	include "ViewHWInterface.h"
 #	include "DWindowHWInterface.h"
#endif


ScreenManager* gScreenManager;


class ScreenChangeListener : public HWInterfaceListener {
public:
								ScreenChangeListener(ScreenManager& manager,
									Screen* screen);

private:
virtual	void					ScreenChanged(HWInterface* interface);

			ScreenManager&		fManager;
			Screen*				fScreen;
};


ScreenChangeListener::ScreenChangeListener(ScreenManager& manager,
	Screen* screen)
	:
	fManager(manager),
	fScreen(screen)
{
}


void
ScreenChangeListener::ScreenChanged(HWInterface* interface)
{
	fManager.ScreenChanged(fScreen);
}


ScreenManager::ScreenManager()
	:
	BLooper("screen manager"),
	fScreenList(4),
	fDrmInterface(NULL),
	fSyncLock("output sync")
{
#ifdef HAIKU_TARGET_PLATFORM_LIBBE_TEST
#	if defined(USE_DIRECT_WINDOW_TEST_MODE)
	_AddHWInterface(new DWindowHWInterface());
#	else
	_AddHWInterface(new ViewHWInterface());
#	endif
#else
	_ScanDrivers();
	#ifndef __VOS__
	// turn on node monitoring the graphics driver directory
	BEntry entry("/dev/graphics");
	node_ref nodeRef;
	if (entry.InitCheck() == B_OK && entry.GetNodeRef(&nodeRef) == B_OK)
		watch_node(&nodeRef, B_WATCH_DIRECTORY, this);
#endif
#endif
}


ScreenManager::~ScreenManager()
{
}


Screen*
ScreenManager::ScreenAt(int32 index) const
{
	if (!IsLocked())
		debugger("Called ScreenManager::ScreenAt() without lock!");

	screen_item* item = fScreenList.ItemAt(index);
	if (item != NULL)
		return item->screen.Get();

	return NULL;
}


int32
ScreenManager::CountScreens() const
{
	if (!IsLocked())
		debugger("Called ScreenManager::CountScreens() without lock!");

	return fScreenList.CountItems();
}


Screen*
ScreenManager::ScreenByID(int32 id) const
{
	if (!IsLocked())
		debugger("Called ScreenManager::ScreenByID() without lock!");

	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);
		if (item->screen->ID() == id)
			return item->screen.Get();
	}

	return NULL;
}


status_t
ScreenManager::AddVirtualOutput(uint16 width, uint16 height, const char* name,
	bool testPattern, int32* id)
{
	BAutolock locker(this);

	int32 nextID = _NextID();

	VirtualOutput* interface = new(nothrow) VirtualOutput(width, height, name);
	if (interface == NULL)
		return B_NO_MEMORY;

	if (testPattern)
		interface->SetTestPattern(true);

	screen_item* item = _AddHWInterface(interface, nextID);
	if (item == NULL) {
		delete interface;
		return B_ERROR;
	}

	item->isVirtual = true;

	if (id != NULL)
		*id = item->screen->ID();

	return B_OK;
}


status_t
ScreenManager::RemoveVirtualOutput(int32 id)
{
	BAutolock locker(this);

	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);
		if (item->screen->ID() != id)
			continue;

		if (!item->isVirtual)
			return B_NOT_ALLOWED;
		if (item->owner != NULL)
			return B_BUSY;

		Screen* screen = item->screen.Get();
		HWInterface* interface = screen->HWInterface();

		fScreenList.RemoveItem(item);
		if (interface != NULL)
			screen->HWInterface()->RemoveListener(item->listener.Get());
		screen->Shutdown();
		delete screen;
		delete item;
		return B_OK;
	}

	return B_BAD_VALUE;
}


status_t
ScreenManager::AcquireScreens(ScreenOwner* owner, int32* wishList,
	int32 wishCount, const char* target, bool force, ScreenList& list)
{
	BAutolock locker(this);
	int32 added = 0;

	// TODO: don't ignore the wish list

	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);

		// A headless output is not part of the desktop.
		if (item->isVirtual)
			continue;

		if (item->owner == NULL && list.AddItem(item->screen.Get())) {
			item->owner = owner;
			added++;
		}
	}
	return added > 0 ? B_OK : B_ENTRY_NOT_FOUND;
}


void
ScreenManager::ReleaseScreens(ScreenList& list)
{
	BAutolock locker(this);

	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);

		for (int32 j = 0; j < list.CountItems(); j++) {
			Screen* screen = list.ItemAt(j);

			if (item->screen.Get() == screen)
				item->owner = NULL;
		}
	}
}


void
ScreenManager::ScreenChanged(Screen* screen)
{
	BAutolock locker(this);

	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);
		// Desktop::Init() restores the stored rotation before the screen
		// is acquired, so there may be no owner yet.
		if (item->screen.Get() == screen && item->owner != NULL)
			item->owner->ScreenChanged(screen);
	}
}


void
ScreenManager::_ScanDrivers()
{
	HWInterface* interface = NULL;

	// Eventually we will loop through drivers until
	// one can't initialize in order to support multiple monitors.
	// For now, we'll just load one and be done with it.

	// ToDo: to make monitoring the driver directory useful, we need more
	//	power and data here, and should do the scanning on our own

	#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
	bool initDrivers = true;
	while (initDrivers) {
	#ifndef __VOS__
		interface = new AccelerantHWInterface();
	#else
		//interface = new SDLInterface();
		DrmHWInterface* drm = new(nothrow) DrmHWInterface();
		if (drm != NULL && drm->InitCheck() == B_OK) {
			interface = drm;
			fDrmInterface = drm;
		} else {
			fprintf(stderr, "DRM backend failed, falling back to FBDev\n");
			delete drm;
			interface = new(nothrow) FBDevHWInterface();
		}
#endif

		if (_AddHWInterface(interface) == NULL)
			fDrmInterface = NULL;
		initDrivers = false;
	}
#ifdef __VOS__
	_SyncOutputs();
#endif
#endif
}


void
ScreenManager::GetOutputs(BObjectList<output_info, true>& outputs)
{
#if !defined(HAIKU_TARGET_PLATFORM_LIBBE_TEST) && defined(__VOS__)
	if (fDrmInterface == NULL)
		return;

	std::vector<DrmHWInterface::OutputInfo> list;
	fDrmInterface->ListOutputs(list);

	BAutolock locker(this);

	for (size_t i = 0; i < list.size(); i++) {
		output_info* info = new(nothrow) output_info;
		if (info == NULL)
			return;

		info->name = list[i].name;
		info->enabled = !list[i].disabled;
		info->first = list[i].first;
		info->screen = -1;
		for (int32 j = 0; j < fScreenList.CountItems(); j++) {
			screen_item* item = fScreenList.ItemAt(j);
			if (list[i].first
					? item->screen->HWInterface() == fDrmInterface
					: item->connector == list[i].connector)
				info->screen = item->screen->ID();
		}
		outputs.AddItem(info);
	}
#endif
}


status_t
ScreenManager::SetOutputEnabled(const char* name, bool enabled)
{
#if !defined(HAIKU_TARGET_PLATFORM_LIBBE_TEST) && defined(__VOS__)
	if (fDrmInterface == NULL)
		return B_NOT_SUPPORTED;

	std::vector<DrmHWInterface::OutputInfo> list;
	fDrmInterface->ListOutputs(list);
	if (!list.empty() && list[0].first && list[0].name == name)
		return B_NOT_ALLOWED;

	{
		BAutolock syncLocker(fSyncLock);

		for (int32 i = fDisabledOutputs.CountItems(); i-- > 0;) {
			if (*fDisabledOutputs.ItemAt(i) == name)
				delete fDisabledOutputs.RemoveItemAt(i);
		}
		if (!enabled)
			fDisabledOutputs.AddItem(new BString(name));
	}

	_SyncOutputs();
	return B_OK;
#else
	return B_NOT_SUPPORTED;
#endif
}


// Ids must stay unique for the life of the server: removals shrink the
// list, so counting items would eventually collide.
int32
ScreenManager::_NextID() const
{
	int32 nextID = 0;
	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		int32 screenID = fScreenList.ItemAt(i)->screen->ID();
		if (screenID >= nextID)
			nextID = screenID + 1;
	}
	return nextID;
}


/*!	Gives every connected output beside the first one a screen and takes
	the screen of an unplugged one away. The owner is called without our
	lock: it takes the window lock, and holds that when it calls us.
*/
void
ScreenManager::_SyncOutputs()
{
#if !defined(HAIKU_TARGET_PLATFORM_LIBBE_TEST) && defined(__VOS__)
	if (fDrmInterface == NULL)
		return;

	BAutolock syncLocker(fSyncLock);

	std::vector<DrmHWInterface::OutputInfo> outputs;
	fDrmInterface->ListOutputs(outputs);
	for (size_t i = 0; i < outputs.size(); i++) {
		bool disabled = false;
		for (int32 j = 0; j < fDisabledOutputs.CountItems(); j++) {
			if (*fDisabledOutputs.ItemAt(j) == outputs[i].name)
				disabled = true;
		}
		if (disabled != outputs[i].disabled) {
			fDrmInterface->SetOutputDisabled(outputs[i].connector,
				disabled);
		}
	}

	std::vector<uint32> connected;
	fDrmInterface->GetConnectedOutputs(connected);

	// gone
	while (true) {
		BAutolock locker(this);

		screen_item* gone = NULL;
		for (int32 i = 0; i < fScreenList.CountItems(); i++) {
			screen_item* item = fScreenList.ItemAt(i);
			if (item->connector != 0
				&& std::find(connected.begin(), connected.end(),
					item->connector) == connected.end()) {
				gone = item;
				break;
			}
		}
		if (gone == NULL)
			break;

		Screen* screen = gone->screen.Get();
		ScreenOwner* owner = gone->owner;
		const uint32 connector = gone->connector;

		// Still listed and owned, so nobody else acquires it meanwhile.
		locker.Unlock();
		if (owner != NULL)
			owner->ScreenRemoved(screen);
		locker.Lock();

		fScreenList.RemoveItem(gone, false);
		screen->HWInterface()->RemoveListener(gone->listener.Get());
		screen->Shutdown();
		locker.Unlock();

		fDrmInterface->RemoveOutput(connector);
		delete gone;
	}

	// new
	for (size_t i = 0; i < connected.size(); i++) {
		BAutolock locker(this);

		bool known = false;
		ScreenOwner* owner = NULL;
		for (int32 j = 0; j < fScreenList.CountItems(); j++) {
			screen_item* item = fScreenList.ItemAt(j);
			if (item->connector == connected[i])
				known = true;
			if (item->screen->HWInterface() == fDrmInterface)
				owner = item->owner;
		}
		if (known)
			continue;

		HWInterface* interface = fDrmInterface->AddOutput(connected[i]);
		if (interface == NULL)
			continue;

		// The same connector gets the same id every time: the stored
		// place of a screen is found by it.
		int32 id = -1;
		for (size_t j = 0; j < outputs.size(); j++) {
			if (outputs[j].connector == connected[i])
				id = outputs[j].index + 1;
		}
		if (id < 0 || ScreenByID(id) != NULL)
			id = _NextID();

		screen_item* item = _AddHWInterface(interface, id);
		if (item == NULL) {
			fDrmInterface->RemoveOutput(connected[i]);
			continue;
		}

		item->connector = connected[i];
		item->owner = owner;
		Screen* screen = item->screen.Get();

		locker.Unlock();
		if (owner != NULL)
			owner->ScreenAdded(screen);
	}

	// resized
	for (size_t i = 0; i < connected.size(); i++) {
		if (!fDrmInterface->TakeOutputModeChange(connected[i]))
			continue;

		BAutolock locker(this);

		Screen* screen = NULL;
		ScreenOwner* owner = NULL;
		for (int32 j = 0; j < fScreenList.CountItems(); j++) {
			screen_item* item = fScreenList.ItemAt(j);
			if (item->connector == connected[i]) {
				screen = item->screen.Get();
				owner = item->owner;
			}
		}

		locker.Unlock();
		if (screen != NULL && owner != NULL)
			owner->ScreenChanged(screen);
	}
#endif
}


ScreenManager::screen_item*
ScreenManager::_AddHWInterface(HWInterface* interface, int32 id)
{
	if (id < 0)
		id = fScreenList.CountItems();

	ObjectDeleter<Screen> screen(new(nothrow) Screen(interface, id));
	if (!screen.IsSet()) {
		delete interface;
		return NULL;
	}

	// The interface is now owned by the screen

	if (screen->Initialize() >= B_OK) {
		screen_item* item = new(nothrow) screen_item;

		if (item != NULL) {
			item->screen.SetTo(screen.Detach());
			item->owner = NULL;
			item->isVirtual = false;
			item->connector = 0;
			item->listener.SetTo(
				new(nothrow) ScreenChangeListener(*this, item->screen.Get()));
			if (item->listener.IsSet()
				&& interface->AddListener(item->listener.Get())) {
				if (fScreenList.AddItem(item))
					return item;

				interface->RemoveListener(item->listener.Get());
			}

			delete item;
		}
	}

	return NULL;
}


void
ScreenManager::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_NODE_MONITOR:
			// TODO: handle notification
			break;

		case kMsgOutputsChanged:
			_SyncOutputs();
			break;

		default:
			BHandler::MessageReceived(message);
	}
}
