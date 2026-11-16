/*
 * Copyright 2001-2009, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Axel Dörfler, axeld@pinc-software.de
 */


/*!	Whenever a ServerBitmap associated with a client-side BBitmap needs to be
	created or destroyed, the BitmapManager needs to handle it. It takes care of
	all memory management related to them.
*/


#include "BitmapManager.h"

#include "ClientMemoryAllocator.h"
#include "HWInterface.h"
#include "Overlay.h"
#include "ServerApp.h"
#include "ServerBitmap.h"
#include "ServerProtocol.h"
#include "ServerTokenSpace.h"

#include <BitmapPrivate.h>
#include <GbmArea.h>
#include <ObjectList.h>
#include <video_overlay.h>

#include <AppDefs.h>
#include <Autolock.h>
#include <Bitmap.h>
#include <Message.h>

#ifdef HAVE_GBM
#include <gbm.h>
#endif

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

using std::nothrow;


//! The one and only bitmap manager for the server, created by the AppServer
BitmapManager *gBitmapManager = NULL;


#ifdef HAVE_GBM
/*!	Attempts to back the bitmap with a GBM buffer object: CPU mapped for
	the server, dma-buf exported into a cloneable area so the client
	maps the very same pages through the ordinary bitmap area protocol.
	Returns the buffer pointer on success (and hands the GbmMemory to
	the caller), or NULL when any step fails — including devices that
	cannot give a linear, CPU mappable buffer — so the caller can fall
	through to the shared-memory path. The flag stays a hint.
*/
static uint8*
_AllocateGbmBuffer(struct gbm_device* device, ServerBitmap* bitmap,
	GbmMemory** _memory, uint32* _stride)
{
	if (device == NULL)
		return NULL;

	uint32 format;
	switch (bitmap->ColorSpace()) {
		case B_RGBA32:
			// BGRA byte order in memory on little endian
			format = GBM_FORMAT_ARGB8888;
			break;
		case B_RGB32:
			format = GBM_FORMAT_XRGB8888;
			break;
		default:
			// No GBM format for this color space; fall through
			return NULL;
	}

	int32 width = bitmap->Width();
	int32 height = bitmap->Height();
	if (width <= 0 || height <= 0)
		return NULL;

	// LINEAR keeps the server blitters and the client CPU access valid
	// through the map. No WRITE: Mesa takes it as a dumb cursor buffer
	// request and refuses anything else. SCANOUT lets planes take it.
	struct gbm_bo* bo = gbm_bo_create(device, width, height, format,
		GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT | GBM_BO_USE_LINEAR);
	if (bo == NULL) {
		bo = gbm_bo_create(device, width, height, format,
			GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);
	}
	if (bo == NULL)
		return NULL;

	if (gbm_bo_get_modifier(bo) != 0) {
		static bool reported = false;
		if (!reported) {
			reported = true;
			fprintf(stderr, "app_server: gbm buffer modifier %#llx is not "
				"linear\n", (unsigned long long)gbm_bo_get_modifier(bo));
		}
	}

	uint32 stride = 0;
	void* mapData = NULL;
	void* map = gbm_bo_map(bo, 0, 0, (uint32)width, (uint32)height,
		GBM_BO_TRANSFER_READ | GBM_BO_TRANSFER_WRITE, &stride, &mapData);
	if (map == NULL) {
		gbm_bo_destroy(bo);
		return NULL;
	}

	int fd = gbm_bo_get_fd(bo);
	if (fd < 0) {
		gbm_bo_unmap(bo, mapData);
		gbm_bo_destroy(bo);
		return NULL;
	}

	// The map's stride belongs to its copy; the pages use the bo's own.
	stride = gbm_bo_get_stride(bo);
	size_t size = (size_t)stride * (size_t)height;
	void* areaBase = NULL;
	area_id area = _kern_create_gbm_area("bitmap gbm buffer", &areaBase,
		B_ANY_ADDRESS, size, B_NO_LOCK,
		B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA, fd);

	// The area keeps its own reference on the dma-buf
	close(fd);

	if (area < B_OK) {
		gbm_bo_unmap(bo, mapData);
		gbm_bo_destroy(bo);
		return NULL;
	}

	GbmMemory* memory = new(std::nothrow) GbmMemory;
	// The painter reads the dma-buf pages themselves; the gbm map may be
	// a private copy the GPU never writes.
	uint8* base = (uint8*)areaBase;
	if (memory == NULL || memory->Adopt(bo, area, base, mapData) != B_OK) {
		delete memory;
		delete_area(area);
		gbm_bo_unmap(bo, mapData);
		gbm_bo_destroy(bo);
		return NULL;
	}

	// The device supplied stride may exceed the format minimum, the
	// same way the overlay path adopts the hardware bytes-per-row.
	*_stride = stride;
	*_memory = memory;
	return base;
}
#endif	// HAVE_GBM


int
compare_app_pointer(const ServerApp* a, const ServerApp* b)
{
	return (addr_t)a - (addr_t)b;
}


//	#pragma mark -


//! Sets up stuff to be ready to allocate space for bitmaps
BitmapManager::BitmapManager()
	:
	fBitmapList(1024),
	fLock("BitmapManager Lock")
{
}


//! Deallocates everything associated with the manager
BitmapManager::~BitmapManager()
{
	int32 count = fBitmapList.CountItems();
	for (int32 i = 0; i < count; i++)
		delete (ServerBitmap*)fBitmapList.ItemAt(i);
}


/*!	\brief Allocates a new ServerBitmap.

	\param bounds Size of the bitmap
	\param space Color space of the bitmap
	\param flags Bitmap flags as defined in Bitmap.h
	\param bytesPerRow Number of bytes per row.
	\param screen Screen id of the screen associated with it. Unused.
	\return A new ServerBitmap or NULL if unable to allocate one.
*/
ServerBitmap*
BitmapManager::CreateBitmap(ClientMemoryAllocator* allocator,
	HWInterface& hwInterface, BRect bounds, color_space space, uint32 flags,
	int32 bytesPerRow, int32 screen, uint8* _allocationFlags,
	const BRect* desktopFrame)
{
	BAutolock locker(fLock);
	if (!locker.IsLocked())
		return NULL;

	overlay_token overlayToken = NULL;

	if (flags & B_BITMAP_WILL_OVERLAY) {
		if (!hwInterface.CheckOverlayRestrictions(bounds.IntegerWidth() + 1,
				bounds.IntegerHeight() + 1, space))
			return NULL;

		if (flags & B_BITMAP_RESERVE_OVERLAY_CHANNEL) {
			overlayToken = hwInterface.AcquireOverlayChannel();
			if (overlayToken == NULL)
				return NULL;
		}
	}

	ServerBitmap* bitmap = new(std::nothrow) ServerBitmap(bounds, space, flags,
		bytesPerRow);
	if (bitmap == NULL) {
		if (overlayToken != NULL)
			hwInterface.ReleaseOverlayChannel(overlayToken);

		return NULL;
	}

	uint8* buffer = NULL;

#ifdef HAVE_GBM
	// GBM-backed bitmap (zero-copy dma-buf pages mapped into the client
	// through the ordinary area protocol). Any failure leaves buffer
	// NULL and the allocation falls through to the paths below, so the
	// flag is a hint, never a hard requirement.
	GbmMemory* gbmMemory = NULL;
	if ((flags & B_BITMAP_SUPPORTS_GBM) != 0
		&& (flags & B_BITMAP_WILL_OVERLAY) == 0) {
		uint32 gbmStride = 0;
		// On the card that scans the frame out; any failure there falls
		// back to the first card.
		struct gbm_device* first = hwInterface.GbmDevice();
		struct gbm_device* device = desktopFrame != NULL
			? hwInterface.GbmDeviceFor(*desktopFrame) : first;
		buffer = _AllocateGbmBuffer(device, bitmap, &gbmMemory, &gbmStride);
		if (buffer == NULL && device != first) {
			buffer = _AllocateGbmBuffer(first, bitmap, &gbmMemory,
				&gbmStride);
		}
		if (buffer != NULL) {
			bitmap->fMemory = gbmMemory;
			bitmap->fBytesPerRow = (int32)gbmStride;

			if (_allocationFlags)
				*_allocationFlags = kGbm;
		}
	}
#endif

	if (buffer != NULL) {
		// already backed above (GBM path)
	} else if (flags & B_BITMAP_WILL_OVERLAY) {
		Overlay* overlay = new(std::nothrow) Overlay(hwInterface, bitmap,
			overlayToken);

		overlay_client_data* clientData = NULL;

		if (overlay != NULL && overlay->InitCheck() == B_OK) {
			// allocate client memory to communicate the overlay semaphore
			// and buffer location to the BBitmap
			clientData = (overlay_client_data*)bitmap->fClientMemory.Allocate(
				allocator, sizeof(overlay_client_data));
		}

		if (clientData != NULL) {
			overlay->SetClientData(clientData);

			bitmap->fMemory = &bitmap->fClientMemory;
			bitmap->SetOverlay(overlay);
			bitmap->fBytesPerRow = overlay->OverlayBuffer()->bytes_per_row;

			buffer = (uint8*)overlay->OverlayBuffer()->buffer;
			if (_allocationFlags)
				*_allocationFlags = kFramebuffer;
		} else
			delete overlay;
	} else if (allocator != NULL) {
		// standard bitmaps
		buffer = (uint8*)bitmap->fClientMemory.Allocate(allocator,
			bitmap->BitsLength());
		if (buffer != NULL) {
			bitmap->fMemory = &bitmap->fClientMemory;

			if (_allocationFlags)
				*_allocationFlags = kAllocator;
		}
	} else {
		// server side only bitmaps
		buffer = (uint8*)malloc(bitmap->BitsLength());
		if (buffer != NULL) {
			bitmap->fMemory = NULL;

			if (_allocationFlags)
				*_allocationFlags = kHeap;
		}
	}

	bool success = false;
	if (buffer != NULL) {
		success = fBitmapList.AddItem(bitmap);
		if (success && bitmap->Overlay() != NULL) {
			success = fOverlays.AddItem(bitmap);
			if (!success)
				fBitmapList.RemoveItem(bitmap);
		}
	}

	if (success) {
		bitmap->fBuffer = buffer;
		bitmap->fToken = gTokenSpace.NewToken(kBitmapToken, bitmap);
		// NOTE: the client handles clearing to white in case the flags
		// indicate this is needed
	} else {
		// Allocation failed for buffer or bitmap list. Only the
		// plain heap path owns a malloc'd buffer; area- and
		// GBM-backed buffers are released with their memory object
		// when the bitmap is deleted.
		if (bitmap->fMemory == NULL)
			free(buffer);
		delete bitmap;
		bitmap = NULL;
	}

	return bitmap;
}


ServerBitmap*
BitmapManager::CloneFromClient(area_id clientArea, size_t areaOffset,
	BRect bounds, color_space space, uint32 flags, int32 bytesPerRow)
{
	BAutolock locker(fLock);
	if (!locker.IsLocked())
		return NULL;
	BReference<ServerBitmap> bitmap(new(std::nothrow) ServerBitmap(bounds, space, flags,
		bytesPerRow), true);
	if (bitmap == NULL)
		return NULL;

	ClonedAreaMemory* memory = new(std::nothrow) ClonedAreaMemory;
	if (memory == NULL) {
		return NULL;
	}
	int8* buffer = (int8*)memory->Clone(clientArea, areaOffset);
	if (buffer == NULL) {
		delete memory;
		return NULL;
	}

	bitmap->fMemory = memory;
	bitmap->fBuffer = memory->Address();
	bitmap->fToken = gTokenSpace.NewToken(kBitmapToken, bitmap);
	return bitmap.Detach();
}


/*!	\brief Called when a ServerBitmap is deleted.
*/
void
BitmapManager::BitmapRemoved(ServerBitmap* bitmap)
{
	BAutolock locker(fLock);
	if (!locker.IsLocked())
		return;

	gTokenSpace.RemoveToken(bitmap->Token());

	if (bitmap->Overlay() != NULL)
		fOverlays.RemoveItem(bitmap);

	fBitmapList.RemoveItem(bitmap);
}


void
BitmapManager::SuspendOverlays()
{
	BAutolock locker(fLock);
	if (!locker.IsLocked())
		return;

	// first, tell all applications owning an overlay to release their locks

	BObjectList<ServerApp> apps;
	for (int32 i = 0; i < fOverlays.CountItems(); i++) {
		ServerBitmap* bitmap = (ServerBitmap*)fOverlays.ItemAt(i);
		apps.BinaryInsert(bitmap->Owner(), &compare_app_pointer);
	}
	for (int32 i = 0; i < apps.CountItems(); i++) {
		BMessage notify(B_RELEASE_OVERLAY_LOCK);
		apps.ItemAt(i)->SendMessageToClient(&notify);
	}

	// suspend overlays

	for (int32 i = 0; i < fOverlays.CountItems(); i++) {
		ServerBitmap* bitmap = (ServerBitmap*)fOverlays.ItemAt(i);
		bitmap->Overlay()->Suspend(bitmap, false);
	}
}


void
BitmapManager::ResumeOverlays()
{
	BAutolock locker(fLock);
	if (!locker.IsLocked())
		return;

	// first, tell all applications owning an overlay that
	// they can reacquire their locks

	BObjectList<ServerApp> apps;
	for (int32 i = 0; i < fOverlays.CountItems(); i++) {
		ServerBitmap* bitmap = (ServerBitmap*)fOverlays.ItemAt(i);
		apps.BinaryInsert(bitmap->Owner(), &compare_app_pointer);
	}
	for (int32 i = 0; i < apps.CountItems(); i++) {
		BMessage notify(B_RELEASE_OVERLAY_LOCK);
		apps.ItemAt(i)->SendMessageToClient(&notify);
	}

	// resume overlays

	for (int32 i = 0; i < fOverlays.CountItems(); i++) {
		ServerBitmap* bitmap = (ServerBitmap*)fOverlays.ItemAt(i);

		bitmap->Overlay()->Resume(bitmap);
	}
}

