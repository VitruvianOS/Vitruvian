/*
 * Copyright 2005, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */
#ifndef SCREEN_MANAGER_H
#define SCREEN_MANAGER_H


#include <AutoDeleter.h>
#include <Looper.h>
#include <ObjectList.h>


class BMessage;

class DrawingEngine;
class HWInterface;
class HWInterfaceListener;
class Screen;


typedef BObjectList<Screen> ScreenList;


class ScreenOwner {
	public:
		virtual ~ScreenOwner() {};
		virtual void	ScreenRemoved(Screen* screen) = 0;
		virtual void	ScreenAdded(Screen* screen) = 0;
		virtual void	ScreenChanged(Screen* screen) = 0;

		virtual bool	ReleaseScreen(Screen* screen) = 0;
};


class ScreenManager : public BLooper {
	public:
		ScreenManager();
		virtual ~ScreenManager();

		Screen*			ScreenAt(int32 index) const;
		Screen*			ScreenByID(int32 id) const;
		int32			CountScreens() const;

		// Headless outputs owned by the manager; ids never collide with
		// or reuse the ids of other screens. Refused while acquired.
		status_t		AddVirtualOutput(uint16 width, uint16 height,
							const char* name, bool testPattern, int32* id);
		status_t		RemoveVirtualOutput(int32 id);

		status_t		AcquireScreens(ScreenOwner* owner, int32* wishList,
							int32 wishCount, const char* target, bool force,
							ScreenList& list);
		void			ReleaseScreens(ScreenList& list);

		void			ScreenChanged(Screen* screen);

		virtual void	MessageReceived(BMessage* message);

	private:
		struct screen_item {
			ObjectDeleter<Screen>	screen;
			ScreenOwner*			owner;
			bool					isVirtual;
			ObjectDeleter<HWInterfaceListener>
									listener;
		};

		void			_ScanDrivers();
		screen_item*	_AddHWInterface(HWInterface* interface,
							int32 id = -1);

		BObjectList<screen_item, true>	fScreenList;
};

extern ScreenManager *gScreenManager;

#endif	/* SCREEN_MANAGER_H */
