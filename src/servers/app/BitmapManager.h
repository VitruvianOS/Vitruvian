/*
 * Copyright 2001-2009, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 */
#ifndef BITMAP_MANAGER_H
#define BITMAP_MANAGER_H


#include <GraphicsDefs.h>
#include <List.h>
#include <Locker.h>
#include <OS.h>
#include <SupportDefs.h>
#include <Rect.h>


class ClientMemoryAllocator;
class HWInterface;
class ServerBitmap;


class BitmapManager {
public:
								BitmapManager();
	virtual						~BitmapManager();

			ServerBitmap*		CreateBitmap(ClientMemoryAllocator* allocator,
									HWInterface& hwInterface, BRect bounds,
									color_space space, uint32 flags,
									int32 bytesPerRow = -1,
									int32 screen = B_MAIN_SCREEN_ID.id,
									uint8* _allocationFlags = NULL,
									const BRect* desktopFrame = NULL,
									bool scanout = true,
									// A GPU-only buffer: it may be tiled
									// with one of these. NULL is linear.
									const uint64* modifiers = NULL,
									int32 modifierCount = 0);

			ServerBitmap*		CloneFromClient(area_id clientArea,
									size_t areaOffset, BRect bounds,
									color_space space, uint32 flags,
									int32 bytesPerRow = -1);

			void				BitmapRemoved(ServerBitmap* bitmap);

			void				SuspendOverlays();
			void				ResumeOverlays();

protected:
			BList				fBitmapList;
			BList				fOverlays;
			BLocker				fLock;
};

extern BitmapManager* gBitmapManager;

#endif	/* BITMAP_MANAGER_H */
