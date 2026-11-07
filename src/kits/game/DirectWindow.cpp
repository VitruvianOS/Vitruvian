/*
 * Copyright 2003-2009 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stefano Ceccherini <stefano.ceccherini@gmail.com>
 *		Carwyn Jones <turok2@currantbun.com>
 */


#include <DirectWindow.h>

#include <stdio.h>
#include <string.h>

#include <Screen.h>
#include <SharedEvent.h>

#include <clipping.h>
#include <AppServerLink.h>
#include <ApplicationPrivate.h>
#include <DirectWindowPrivate.h>
#include <ServerProtocol.h>
#include <ServerMemoryAllocator.h>


//#define DEBUG		1
#define OUTPUT		printf
//#define OUTPUT	debug_printf



enum dw_status_bits {
	DW_STATUS_AREA_CLONED	 = 0x1,
	DW_STATUS_THREAD_STARTED = 0x2
};


#if DEBUG


static void
print_direct_buffer_state(const direct_buffer_state &state)
{
	char string[128];
	int modeState = state & B_DIRECT_MODE_MASK;
	if (modeState == B_DIRECT_START)
		strcpy(string, "B_DIRECT_START");
	else if (modeState == B_DIRECT_MODIFY)
		strcpy(string, "B_DIRECT_MODIFY");
	else if (modeState == B_DIRECT_STOP)
		strcpy(string, "B_DIRECT_STOP");

	if (state & B_CLIPPING_MODIFIED)
		strcat(string, " | B_CLIPPING_MODIFIED");
	if (state & B_BUFFER_RESIZED)
		strcat(string, " | B_BUFFER_RESIZED");
	if (state & B_BUFFER_MOVED)
		strcat(string, " | B_BUFFER_MOVED");
	if (state & B_BUFFER_RESET)
		strcat(string, " | B_BUFFER_RESET");

	OUTPUT("direct_buffer_state: %s\n", string);
}


static void
print_direct_driver_state(const direct_driver_state &state)
{
	if (state == 0)
		return;

	char string[64];
	if (state == B_DRIVER_CHANGED)
		strcpy(string, "B_DRIVER_CHANGED");
	else if (state == B_MODE_CHANGED)
		strcpy(string, "B_MODE_CHANGED");

	OUTPUT("direct_driver_state: %s\n", string);
}


#if DEBUG > 1


static void
print_direct_buffer_layout(const buffer_layout &layout)
{
	char string[64];
	if (layout == B_BUFFER_NONINTERLEAVED)
		strcpy(string, "B_BUFFER_NONINTERLEAVED");
	else
		strcpy(string, "unknown");

	OUTPUT("layout: %s\n", string);
}


static void
print_direct_buffer_orientation(const buffer_orientation &orientation)
{
	char string[64];
	switch (orientation) {
		case B_BUFFER_TOP_TO_BOTTOM:
			strcpy(string, "B_BUFFER_TOP_TO_BOTTOM");
			break;
		case B_BUFFER_BOTTOM_TO_TOP:
			strcpy(string, "B_BUFFER_BOTTOM_TO_TOP");
			break;
		default:
			strcpy(string, "unknown");
			break;
	}

	OUTPUT("orientation: %s\n", string);
}


#endif	// DEBUG > 2


static void
print_direct_buffer_info(const direct_buffer_info &info)
{
	print_direct_buffer_state(info.buffer_state);
	print_direct_driver_state(info.driver_state);

#	if DEBUG > 1
	OUTPUT("bits: %p\n", info.bits);
	OUTPUT("bytes_per_row: %ld\n", info.bytes_per_row);
	OUTPUT("bits_per_pixel: %lu\n", info.bits_per_pixel);
	OUTPUT("pixel_format: %d\n", info.pixel_format);
	print_direct_buffer_layout(info.layout);
	print_direct_buffer_orientation(info.orientation);

#		if DEBUG > 2
	// TODO: this won't work correctly with debug_printf()
	printf("CLIPPING INFO:\n");
	printf("clipping_rects count: %ld\n", info.clip_list_count);

	printf("- window_bounds:\n");
	BRegion region;
	region.Set(info.window_bounds);
	region.PrintToStream();

	region.MakeEmpty();
	for (uint32 i = 0; i < info.clip_list_count; i++)
		region.Include(info.clip_list[i]);

	printf("- clip_list:\n");
	region.PrintToStream();
#		endif
#	endif

	OUTPUT("\n");
}


#endif	// DEBUG


//	#pragma mark -


BDirectWindow::BDirectWindow(BRect frame, const char* title, window_type type,
		uint32 flags, uint32 workspace)
	:
	BWindow(frame, title, type, flags, workspace)
{
	_InitData();
}


BDirectWindow::BDirectWindow(BRect frame, const char* title, window_look look,
		window_feel feel, uint32 flags, uint32 workspace)
	:
	BWindow(frame, title, look, feel, flags, workspace)
{
	_InitData();
}


BDirectWindow::~BDirectWindow()
{
	_DisposeData();
}


//	#pragma mark - BWindow API implementation


BArchivable*
BDirectWindow::Instantiate(BMessage* data)
{
	return NULL;
}


status_t
BDirectWindow::Archive(BMessage* data, bool deep) const
{
	return inherited::Archive(data, deep);
}


void
BDirectWindow::Quit()
{
	inherited::Quit();
}


void
BDirectWindow::DispatchMessage(BMessage* message, BHandler* handler)
{
	inherited::DispatchMessage(message, handler);
}


void
BDirectWindow::MessageReceived(BMessage* message)
{
	inherited::MessageReceived(message);
}


void
BDirectWindow::FrameMoved(BPoint newPosition)
{
	inherited::FrameMoved(newPosition);
}


void
BDirectWindow::WorkspacesChanged(uint32 oldWorkspaces, uint32 newWorkspaces)
{
	inherited::WorkspacesChanged(oldWorkspaces, newWorkspaces);
}


void
BDirectWindow::WorkspaceActivated(int32 index, bool state)
{
	inherited::WorkspaceActivated(index, state);
}


void
BDirectWindow::FrameResized(float newWidth, float newHeight)
{
	inherited::FrameResized(newWidth, newHeight);
}


void
BDirectWindow::Minimize(bool minimize)
{
	inherited::Minimize(minimize);
}


void
BDirectWindow::Zoom(BPoint recPosition, float recWidth, float recHeight)
{
	inherited::Zoom(recPosition, recWidth, recHeight);
}


void
BDirectWindow::ScreenChanged(BRect screenFrame, color_space depth)
{
	inherited::ScreenChanged(screenFrame, depth);
}


void
BDirectWindow::MenusBeginning()
{
	inherited::MenusBeginning();
}


void
BDirectWindow::MenusEnded()
{
	inherited::MenusEnded();
}


void
BDirectWindow::WindowActivated(bool state)
{
	inherited::WindowActivated(state);
}


void
BDirectWindow::Show()
{
	inherited::Show();
}


void
BDirectWindow::Hide()
{
	inherited::Hide();
}


BHandler*
BDirectWindow::ResolveSpecifier(BMessage* message, int32 index,
	BMessage* specifier, int32 what, const char* property)
{
	return inherited::ResolveSpecifier(message, index, specifier, what,
		property);
}


status_t
BDirectWindow::GetSupportedSuites(BMessage* data)
{
	return inherited::GetSupportedSuites(data);
}


status_t
BDirectWindow::Perform(perform_code d, void* arg)
{
	return inherited::Perform(d, arg);
}


void
BDirectWindow::task_looper()
{
	inherited::task_looper();
}


BMessage*
BDirectWindow::ConvertToMessage(void* raw, int32 code)
{
	return inherited::ConvertToMessage(raw, code);
}


//	#pragma mark - BDirectWindow specific API


void
BDirectWindow::DirectConnected(direct_buffer_info* info)
{
	// implemented in subclasses
}


status_t
BDirectWindow::GetClippingRegion(BRegion* region, BPoint* origin) const
{
	if (region == NULL)
		return B_BAD_VALUE;

	if (IsLocked() || !_LockDirect())
		return B_ERROR;

	if (!fInDirectConnected) {
		_UnlockDirect();
		return B_ERROR;
	}

	// BPoint's coordinates are floats. We can only work
	// with integers._DaemonStarter
	int32 originX, originY;
	if (origin == NULL) {
		originX = 0;
		originY = 0;
	} else {
		originX = (int32)origin->x;
		originY = (int32)origin->y;
	}

#ifndef HAIKU_TARGET_PLATFORM_DANO
	// Since we are friend of BRegion, we can access its private members.
	// Otherwise, we would need to call BRegion::Include(clipping_rect)
	// for every clipping_rect in our clip_list, and that would be much
	// more overkill than this (tested ).
	if (!region->_SetSize(fBufferDesc->clip_list_count)) {
		_UnlockDirect();
		return B_NO_MEMORY;
	}
	region->fCount = fBufferDesc->clip_list_count;
	region->fBounds = region->_ConvertToInternal(fBufferDesc->clip_bounds);
	for (uint32 c = 0; c < fBufferDesc->clip_list_count; c++) {
		region->fData[c] = region->_ConvertToInternal(
			fBufferDesc->clip_list[c]);
	}

	// adjust bounds by the given origin point
	region->OffsetBy(-originX, -originY);
#endif

	_UnlockDirect();

	return B_OK;

}


status_t
BDirectWindow::SetFullScreen(bool enable)
{
	if (fIsFullScreen == enable)
		return B_OK;

	status_t status = B_ERROR;
	if (Lock()) {
		fLink->StartMessage(AS_DIRECT_WINDOW_SET_FULLSCREEN);
		fLink->Attach<bool>(enable);

		if (fLink->FlushWithReply(status) == B_OK
			&& status == B_OK) {
			fIsFullScreen = enable;
		}
		Unlock();
	}
	return status;
}


bool
BDirectWindow::IsFullScreen() const
{
	return fIsFullScreen;
}


/*static*/ bool
BDirectWindow::SupportsWindowMode(screen_id id)
{
	display_mode mode;
	status_t status = BScreen(id).GetMode(&mode);
	if (status == B_OK)
		return (mode.flags & B_PARALLEL_ACCESS) != 0;

	return false;
}


//	#pragma mark - Private methods


/*static*/ int32
BDirectWindow::_daemon_thread(void* arg)
{
	return static_cast<BDirectWindow*>(arg)->_DirectDaemon();
}


int32
BDirectWindow::_DirectDaemon()
{
	while (!fDaemonKiller) {
		// Posted by the app_server when our clipping region changes,
		// or when our window is moved, resized, etc. etc.
		BSharedEvent disable(fDisableState);
		disable.SetPeer(direct_window_server_liveness(fBufferDesc));
		status_t status;
		do {
			status = disable.Wait();
		} while (status == B_INTERRUPTED);

		if (status == B_BAD_SEM_ID) {
			// The app_server marked the word dead, the window is gone and
			// no stop notification will follow.
			fConnectionEnable = false;
			return 0;
		}
		if (status != B_OK) {
			//fprintf(stderr, "DirectDaemon: failed to wait direct event: %s\n",
				// strerror(status));
			return -1;
		}

		// _DisposeData() posts once to end the loop, without a callback.
		if (fDaemonKiller)
			break;

#if DEBUG
		print_direct_buffer_info(*fBufferDesc);
#endif

		if (_LockDirect()) {
			if ((fBufferDesc->buffer_state & B_DIRECT_MODE_MASK)
					== B_DIRECT_START)
				fConnectionEnable = true;

			if (fBufferDesc->bits == NULL && fBufferDesc->bits_area >= 0) {
				// (re-)clone the bits area
				BPrivate::AppServerLink linkLocker;
					// protects ServerAllocator
				BPrivate::ServerMemoryAllocator* allocator
					= BApplication::Private::ServerAllocator();
				allocator->RemoveArea(fSourceBitsArea);
				fSourceBitsArea = -1;
				area_id localArea;
				uint8* bits;
				if (allocator->AddArea(fBufferDesc->bits_area, localArea, bits,
						(size_t)-1) == B_OK) {
					fBufferDesc->bits = (void*)bits;
					fSourceBitsArea = fBufferDesc->bits_area;
				}
			}

			fInDirectConnected = true;
			DirectConnected(fBufferDesc);
			fInDirectConnected = false;

			if ((fBufferDesc->buffer_state & B_DIRECT_MODE_MASK)
					== B_DIRECT_STOP)
				fConnectionEnable = false;

			_UnlockDirect();
		}

		// The app_server then waits (with a timeout) on this word.
		// If we aren't quick enough to acknowledge, our app
		// will be terminated by the app_server
		BSharedEvent(fAckState).Post();
	}

	return 0;
}


bool
BDirectWindow::_LockDirect() const
{
	// A no-op, the direct daemon changes no shared data.
	return true;
}


void
BDirectWindow::_UnlockDirect() const
{
}


void
BDirectWindow::_InitData()
{
	fConnectionEnable = false;
	fIsFullScreen = false;
	fInDirectConnected = false;

	fInitStatus = 0;

	status_t status = B_ERROR;
	struct direct_window_sync_data syncData;

	fLink->StartMessage(AS_DIRECT_WINDOW_GET_SYNC_DATA);
	if (fLink->FlushWithReply(status) == B_OK && status == B_OK)
		fLink->Read<direct_window_sync_data>(&syncData);

	if (status != B_OK)
		return;


	fSourceDirectArea = syncData.area;

	// the ack word is written from here, so the clone needs write access
	fClonedDirectArea = clone_area("cloned direct area", (void**)&fBufferDesc,
		B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, fSourceDirectArea);

	fSourceBitsArea = -1;

	if (fClonedDirectArea > 0) {
		fInitStatus |= DW_STATUS_AREA_CLONED;

		fDisableState = direct_window_disable_state(fBufferDesc);
		fAckState = direct_window_ack_state(fBufferDesc);

		// The app_server watches this team while it waits for our ack.
		shared_liveness_hold(direct_window_client_liveness(fBufferDesc));

		fDirectDaemonId = spawn_thread(_daemon_thread, "direct daemon",
			B_DISPLAY_PRIORITY, this);

		if (fDirectDaemonId > 0) {
			fDaemonKiller = false;
			if (resume_thread(fDirectDaemonId) == B_OK)
				fInitStatus |= DW_STATUS_THREAD_STARTED;
			else
				kill_thread(fDirectDaemonId);
		}
	}
}


void
BDirectWindow::_DisposeData()
{
	// wait until the connection terminates: we can't destroy
	// the object until the client receives the B_DIRECT_STOP
	// notification, or bad things will happen
	while (fConnectionEnable)
		snooze(50000);

	_LockDirect();

	if (fInitStatus & DW_STATUS_THREAD_STARTED) {
		fDaemonKiller = true;
		// The daemon rechecks fDaemonKiller after this post and exits
		// without another DirectConnected() round.
		BSharedEvent(fDisableState).Post();
		status_t retVal;
		wait_for_thread(fDirectDaemonId, &retVal);
	}


	if (fInitStatus & DW_STATUS_AREA_CLONED) {
		shared_liveness_release(direct_window_client_liveness(fBufferDesc));
		delete_area(fClonedDirectArea);
	}
}


void BDirectWindow::_ReservedDirectWindow1() {}
void BDirectWindow::_ReservedDirectWindow2() {}
void BDirectWindow::_ReservedDirectWindow3() {}
void BDirectWindow::_ReservedDirectWindow4() {}
