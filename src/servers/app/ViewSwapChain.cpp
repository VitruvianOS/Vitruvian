/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "ViewSwapChain.h"

#include <new>
#include <stdio.h>
#include <string.h>

#include <Autolock.h>
#include <PortLink.h>
#include <Message.h>
#include <Bitmap.h>

#include "BitmapManager.h"
#include "Desktop.h"
#include "DrawingEngine.h"
#include "HWInterface.h"
#include "ServerApp.h"
#include "ServerBitmap.h"
#include "ServerWindow.h"
#include "View.h"
#include "VirtualScreen.h"
#include "Window.h"

#include "ClientMemoryAllocator.h"


using namespace BPrivate;


static int32 sNextToken = 0;


struct ViewSwapChain::Generation {
	int32					token;
	int32					width;
	int32					height;
	int32					count;
	size_t					bytes;
	bool					presented;
	bool					held[kSwapChainMaxBuffers];
	BReference<ServerBitmap> buffers[kSwapChainMaxBuffers];
	area_id					stateArea;
	swap_chain_state*		state;

	Generation()
		:
		token(0),
		width(0),
		height(0),
		count(0),
		bytes(0),
		presented(false),
		stateArea(-1),
		state(NULL)
	{
		memset(held, 0, sizeof(held));
	}

	~Generation()
	{
		for (int32 i = 0; i < kSwapChainMaxBuffers; i++) {
			ServerBitmap* bitmap = buffers[i].Get();
			if (bitmap != NULL)
				gBitmapManager->BitmapRemoved(bitmap);
		}
		if (stateArea >= 0)
			delete_area(stateArea);
	}
};


struct ViewSwapChain::Release {
	int32		token;
	int32		index;
	bigtime_t	when;
	int32		period;
};


// Draws the buffer through the server painter, like any bitmap.
class CpuConsumer : public SwapChainConsumer {
public:
	virtual bool Show(const SwapChainFrame& frame)
	{
		if (frame.engine == NULL || frame.bitmap == NULL
				|| frame.clipping == NULL)
			return false;

		const int32 width = frame.bitmap->Width();
		const int32 height = frame.bitmap->Height();
		const BRect bounds(0, 0, width - 1, height - 1);

		if (!frame.engineLocked)
			frame.engine->LockParallelAccess();
		frame.engine->ConstrainClippingRegion(frame.clipping);
		drawing_mode oldMode;
		frame.engine->SetDrawingMode(B_OP_COPY, oldMode);

		const int32 count = frame.damage != NULL
			? frame.damage->CountRects() : 0;
		if (count == 0) {
			frame.engine->DrawBitmap(frame.bitmap, bounds,
				frame.destination, 0);
		} else {
			for (int32 i = 0; i < count; i++) {
				BRect source = frame.damage->RectAt(i) & bounds;
				if (!source.IsValid())
					continue;
				BRect destination = source;
				destination.OffsetBy(frame.destination.left,
					frame.destination.top);
				frame.engine->DrawBitmap(frame.bitmap, source, destination, 0);
			}
		}

		frame.engine->SetDrawingMode(oldMode);
		if (!frame.engineLocked)
			frame.engine->UnlockParallelAccess();
		return true;
	}

	virtual void Hide()
	{
	}
};


ViewSwapChain::ViewSwapChain(ServerApp* app, ServerWindow* window, View* view)
	:
	fApp(app),
	fWindow(window),
	fView(view),
	fLock("view swap chain"),
	fCurrent(NULL),
	fShownGeneration(NULL),
	fShownIndex(-1),
	fConsumer(NULL),
	fCounted(false)
{
}


ViewSwapChain::~ViewSwapChain()
{
	Hide();

	BAutolock locker(fLock);
	fWindow = NULL;
	fView = NULL;
	for (size_t i = 0; i < fGenerations.size(); i++) {
		fApp->ReleaseSwapChain(fGenerations[i]->bytes, false);
		delete fGenerations[i];
	}
	fGenerations.clear();
	if (fCounted)
		fApp->ReleaseSwapChain(0, true);
	delete fConsumer;
}


status_t
ViewSwapChain::Allocate(int32 width, int32 height, int32 count, bool resize)
{
	if (!swap_chain_enabled())
		return B_UNSUPPORTED;
	if (resize && count == 0) {
		BAutolock locker(fLock);
		if (fCurrent != NULL)
			count = fCurrent->count;
	}
	if (width <= 0 || height <= 0 || count < 2
			|| count > kSwapChainMaxBuffers)
		return B_BAD_VALUE;

	::Desktop* desktop = fWindow->Desktop();
	const BRect screen = desktop->VirtualScreen().Frame();
	if (width > screen.IntegerWidth() + 1 || height > screen.IntegerHeight() + 1)
		return B_BAD_VALUE;

	::HWInterface* hardware = desktop->HWInterface();
	if (hardware == NULL)
		return B_NO_INIT;

	BAutolock locker(fLock);
	if (!resize && fCurrent != NULL)
		return B_BUSY;
	if (resize && fCurrent == NULL)
		return B_BAD_VALUE;

	const size_t bytes = (size_t)((width * 4 + 255) & ~255) * height * count;
	if (!fApp->ReserveSwapChain(bytes, !resize))
		return B_NO_MEMORY;

	Generation* generation = new(std::nothrow) Generation;
	if (generation == NULL) {
		fApp->ReleaseSwapChain(bytes, !resize);
		return B_NO_MEMORY;
	}
	generation->bytes = bytes;
	generation->width = width;
	generation->height = height;
	generation->count = count;

	status_t status = B_OK;
	for (int32 i = 0; i < count && status == B_OK; i++) {
		ServerBitmap* bitmap = gBitmapManager->CreateBitmap(NULL, *hardware,
			BRect(0, 0, width - 1, height - 1), B_RGB32,
			B_BITMAP_SUPPORTS_GBM);
		if (bitmap == NULL) {
			status = B_NO_MEMORY;
			break;
		}
		generation->buffers[i].SetTo(bitmap, true);
#ifdef HAVE_GBM
		if (bitmap->GbmBuffer() == NULL)
			status = B_NOT_SUPPORTED;
#else
		status = B_NOT_SUPPORTED;
#endif
	}

	if (status == B_OK) {
		void* address = NULL;
		generation->stateArea = create_area("swap chain state", &address,
			B_ANY_ADDRESS, B_PAGE_SIZE, B_NO_LOCK,
			B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);
		if (generation->stateArea < 0)
			status = generation->stateArea;
		else {
			generation->state = (swap_chain_state*)address;
			memset(generation->state, 0, sizeof(swap_chain_state));
		}
	}

	if (status == B_OK) {
		try {
			fGenerations.push_back(generation);
		} catch (std::bad_alloc&) {
			status = B_NO_MEMORY;
		}
	}

	if (status != B_OK) {
		delete generation;
		fApp->ReleaseSwapChain(bytes, !resize);
		return status;
	}

	generation->token = atomic_add(&sNextToken, 1) + 1;
	fCurrent = generation;
	if (!resize)
		fCounted = true;
	_Prune();
	return B_OK;
}


void
ViewSwapChain::Describe(BPrivate::PortLink& sender) const
{
	BAutolock locker(fLock);
	const Generation* generation = fCurrent;

	sender.Attach<int32>(generation->token);
	sender.Attach<int32>(generation->count);
	sender.Attach<uint32>(Format());
	sender.Attach<area_id>(generation->stateArea);
	for (int32 i = 0; i < generation->count; i++) {
		const ServerBitmap* bitmap = generation->buffers[i].Get();
		sender.Attach<area_id>(bitmap->Area());
		sender.Attach<uint32>(Format());
		sender.Attach<uint64>(kSwapModifierLinear);
		sender.Attach<uint32>((uint32)bitmap->BytesPerRow());
		sender.Attach<uint32>(0);
	}
}


bool
ViewSwapChain::Accepts(int32 token) const
{
	BAutolock locker(fLock);
	return _Find(token) != NULL;
}


uint32
ViewSwapChain::Format() const
{
	return kSwapFormatXRGB8888;
}


void
ViewSwapChain::Present(int32 token, int32 index, const BRegion& damage,
	uint32 flags, DrawingEngine* engine, const BRegion* clipping)
{
	SwapChainFrame frame;
	Generation* generation;

	{
		BAutolock locker(fLock);
		generation = _Find(token);
		if (generation == NULL || index < 0 || index >= generation->count
				|| generation->held[index]) {
			fprintf(stderr, "swap chain: present of buffer %d rejected\n",
				(int)index);
			return;
		}

		generation->held[index] = true;
		generation->presented = true;
		frame.bitmap = generation->buffers[index].Get();
		frame.token = token;
		frame.index = index;
		frame.destination = _Destination(generation);
		frame.damage = &damage;
		frame.engine = engine;
		frame.engineLocked = false;
		frame.clipping = clipping;
		frame.flags = flags;

		if (fConsumer == NULL)
			fConsumer = new(std::nothrow) CpuConsumer;
	}

	// Drawing takes the engine lock; ours is not held meanwhile.
	bool shown = fConsumer != NULL && fConsumer->Show(frame);

	BAutolock locker(fLock);
	Generation* previous = fShownGeneration;
	const int32 previousIndex = fShownIndex;

	if (shown) {
		fShownGeneration = generation;
		fShownIndex = index;
	} else {
		// Nothing was shown, so nothing stays held.
		_Retire(generation, index, system_time());
	}
	if (previous != NULL && shown)
		_Retire(previous, previousIndex, system_time());
	_Prune();
}


void
ViewSwapChain::Drop(int32 token, int32 index)
{
	BAutolock locker(fLock);
	Generation* generation = _Find(token);
	if (generation == NULL || index < 0 || index >= generation->count
			|| generation->held[index])
		return;

	generation->held[index] = true;
	_Retire(generation, index, system_time());
}


void
ViewSwapChain::Redraw(DrawingEngine* engine, const BRegion& region)
{
	BReference<ServerBitmap> bitmap;
	BRect destination;
	int32 index;
	{
		BAutolock locker(fLock);
		if (fShownGeneration == NULL || fShownIndex < 0 || engine == NULL)
			return;

		index = fShownIndex;
		bitmap = fShownGeneration->buffers[index];
		destination = _Destination(fShownGeneration);
	}

	SwapChainFrame frame;
	frame.bitmap = bitmap.Get();
	frame.token = 0;
	frame.index = index;
	frame.destination = destination;
	frame.damage = NULL;
	frame.engine = engine;
	frame.engineLocked = true;
	frame.clipping = &region;
	frame.flags = 0;
	CpuConsumer().Show(frame);
}


void
ViewSwapChain::Hide()
{
	SwapChainConsumer* consumer;
	{
		BAutolock locker(fLock);
		consumer = fConsumer;
	}
	if (consumer != NULL)
		consumer->Hide();
}


void
ViewSwapChain::SendReleases()
{
	std::vector<Release> releases;
	ServerWindow* window;
	{
		BAutolock locker(fLock);
		releases.swap(fReleases);
		window = fWindow;
	}
	if (window == NULL)
		return;

	for (size_t i = 0; i < releases.size(); i++) {
		BMessage message(B_SWAP_CHAIN_RELEASED);
		message.AddInt32("token", releases[i].token);
		message.AddInt32("index", releases[i].index);
		message.AddInt64("when", releases[i].when);
		message.AddInt32("period", releases[i].period);
		window->SendMessageToClient(&message);
	}
}


ViewSwapChain::Generation*
ViewSwapChain::_Find(int32 token) const
{
	if (token == 0)
		return NULL;

	for (size_t i = 0; i < fGenerations.size(); i++) {
		if (fGenerations[i]->token == token)
			return fGenerations[i];
	}
	return NULL;
}


// fLock held.
void
ViewSwapChain::_Retire(Generation* generation, int32 index, bigtime_t when)
{
	generation->held[index] = false;

	int32 period = 0;
	::HWInterface* hardware = fWindow != NULL
		? fWindow->Desktop()->HWInterface() : NULL;
	if (hardware != NULL) {
		display_mode mode;
		hardware->GetMode(&mode);
		const uint64 pixels = (uint64)mode.timing.h_total
			* mode.timing.v_total;
		if (mode.timing.pixel_clock != 0)
			period = (int32)(pixels * 1000 / mode.timing.pixel_clock);
	}

	swap_chain_state* state = generation->state;
	state->releaseTime = when;
	state->refreshPeriod = period;
	__atomic_store_n(&state->buffer[index], (int32)kSwapBufferFree,
		__ATOMIC_RELEASE);

	Release release;
	release.token = generation->token;
	release.index = index;
	release.when = when;
	release.period = period;
	try {
		fReleases.push_back(release);
	} catch (std::bad_alloc&) {
	}
}


// Keeps the current generation, the one on screen and, until the first
// present at the new size, the previous one. fLock held.
void
ViewSwapChain::_Prune()
{
	for (size_t i = fGenerations.size(); i-- > 0;) {
		Generation* generation = fGenerations[i];
		if (generation == fCurrent || generation == fShownGeneration)
			continue;

		// A resize keeps the latest older generation until fCurrent
		// has been presented.
		if (!fCurrent->presented && i + 2 == fGenerations.size())
			continue;

		bool busy = false;
		for (int32 j = 0; j < generation->count; j++)
			busy = busy || generation->held[j];
		if (busy)
			continue;

		fApp->ReleaseSwapChain(generation->bytes, false);
		fGenerations.erase(fGenerations.begin() + i);
		delete generation;
	}
}


BRect
ViewSwapChain::_Destination(const Generation* generation) const
{
	if (fView == NULL)
		return BRect();

	const IntRect bounds = fView->Bounds();
	BRect rect(bounds.left, bounds.top, bounds.left + generation->width - 1,
		bounds.top + generation->height - 1);
	fView->LocalToScreenTransform().Apply(&rect);
	return rect;
}
