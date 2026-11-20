/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SWAP_CHAIN_H
#define _SWAP_CHAIN_H


#include <stdlib.h>

#include <OS.h>
#include <Rect.h>
#include <SupportDefs.h>


class BMessage;
class BView;


// Sent by app_server to the window: token, index, when, period.
enum {
	B_SWAP_CHAIN_RELEASED	= '_SWR'
};


namespace BPrivate {

enum {
	kSwapChainMaxBuffers	= 4,
	kSwapChainMaxDamage		= 16
};

// DRM fourcc and modifier values, spelled out so libbe needs no drm header.
enum {
	kSwapFormatXRGB8888		= 0x34325258,
	kSwapFormatARGB8888		= 0x34325241
};
const uint64 kSwapModifierLinear = 0;
const uint64 kSwapModifierInvalid = 0x00ffffffffffffffULL;

// Present flags.
enum {
	kPresentVsync			= 0x1,
	kPresentHasFence		= 0x2
};

// Create usage flags.
enum {
	kSwapUsageRender		= 0x1,
	// Only the GPU touches the buffers, through the dma-buf: the server may
	// pick a tiled layout from the modifiers the client lists.
	kSwapUsageGpu			= 0x2
};

// Buffer states shared through the state area.
enum {
	kSwapBufferFree			= 0,
	kSwapBufferClient		= 1,
	kSwapBufferShown		= 2
};

struct swap_chain_format {
	uint32	fourcc;
	uint64	modifier;
};

// Lives in a small area the server creates and the client clones.
struct swap_chain_state {
	int32		buffer[kSwapChainMaxBuffers];
	int64		releaseTime;
	int32		refreshPeriod;
	int32		_reserved;
};

struct swap_chain_buffer {
	area_id		area;
	uint32		fourcc;
	uint64		modifier;
	uint32		stride;
	uint32		offset;
	void*		bits;
};

class PortLink;

inline bool
swap_chain_enabled()
{
	const char* value = getenv("VOS_SWAP_CHAIN");
	return value != NULL && atoi(value) == 1;
}


class SwapChain {
public:
								SwapChain();
								~SwapChain();

			status_t			Create(BView* view, int32 width, int32 height,
									int32 count, uint32 usage,
									const swap_chain_format* formats,
									int32 formatCount);
			// A surface has no view: its root view token and link.
			status_t			Create(PortLink* link, int32 viewToken,
									int32 width, int32 height, int32 count,
									uint32 usage,
									const swap_chain_format* formats,
									int32 formatCount);
			status_t			Resize(int32 width, int32 height);
			void				Delete();

			bool				IsValid() const { return fToken != 0; }
			int32				Width() const { return fWidth; }
			int32				Height() const { return fHeight; }
			uint32				Format() const { return fFormat; }
			int32				CountBuffers() const { return fCount; }
			status_t			BufferAt(int32 index,
									swap_chain_buffer* buffer) const;
			// dma-buf of the buffer; the caller closes it.
			int					BufferFd(int32 index) const;

			// Index of a free buffer, B_ERROR on timeout.
			int32				Acquire(bigtime_t timeout = B_INFINITE_TIMEOUT);
			// Takes no window lock; one thread at a time.
			status_t			Present(int32 index, const BRect* damage = NULL,
									int32 damageCount = 0, uint32 flags = 0);

			// Present after the GPU work that wrote the buffer, signalled by
			// the sync_file fd (EGL_ANDROID_native_fence_sync); the fence is
			// put on the dma-buf, which the server waits on before it
			// shows or copies. The fd stays with the caller. Falls back to
			// a plain present, relying on implicit sync, when the
			// kernel cannot take it.
			status_t			PresentWithFence(int32 index, int fenceFd,
									const BRect* damage = NULL,
									int32 damageCount = 0, uint32 flags = 0);

			// Time and period of the latest release.
			bigtime_t			LastReleaseTime() const;
			int32				RefreshPeriod() const;

private:
			status_t			_Create(PortLink* link, int32 width,
									int32 height, int32 count, uint32 usage,
									const swap_chain_format* formats,
									int32 formatCount);
			status_t			_Resize(PortLink* link, int32 width,
									int32 height);
			status_t			_ReadReply(PortLink& link);
			void				_Unmap();
			void				_Abandon(PortLink& link);

			BView*				fView;
			// Set instead of fView for a surface.
			PortLink*			fSurfaceLink;
			int32				fToken;
			int32				fViewToken;
			// Presents go straight to the window port: no window lock.
			PortLink*			fPresentLink;
			int32				fWidth;
			int32				fHeight;
			int32				fCount;
			uint32				fFormat;
			uint32				fUsage;
			int32				fNext;
			area_id				fStateArea;
			swap_chain_state*	fState;
			swap_chain_buffer	fBuffers[kSwapChainMaxBuffers];
			// Requested formats, kept for Resize().
			swap_chain_format	fFormats[4];
			int32				fFormatCount;
};

}	// namespace BPrivate


#endif	// _SWAP_CHAIN_H
