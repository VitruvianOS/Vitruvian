/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "ViewSwapChain.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <unistd.h>
#include <sys/ioctl.h>
#if defined(__x86_64__) || defined(__i386__)
#	include <immintrin.h>
#	define SWAP_CHAIN_X86 1
#endif
#ifdef HAVE_GBM
#	include <gbm.h>
#	include <linux/dma-buf.h>
#endif

#include <Autolock.h>
#include <PortLink.h>
#include <Message.h>
#include <Bitmap.h>

#include "BitmapManager.h"
#include "Desktop.h"
#include "DrawingEngine.h"
#include "HWInterface.h"
#include "OverlayPlane.h"
#include "ServerApp.h"
#include "ServerBitmap.h"
#include "ServerWindow.h"
#include "SwapChainGpuCopy.h"
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


SwapChainCpuRead::SwapChainCpuRead(ServerBitmap* bitmap)
	:
	fFd(-1),
	fWait(0)
{
#ifdef HAVE_GBM
	const bigtime_t start = system_time();
	GbmMemory* memory = bitmap != NULL ? bitmap->GbmBuffer() : NULL;
	if (memory == NULL)
		return;

	fFd = memory->ExportFd();
	if (fFd < 0)
		return;

	// Implicit fences: wait for the pending writer, bounded.
	struct dma_buf_export_sync_file exported;
	exported.flags = DMA_BUF_SYNC_READ;
	exported.fd = -1;
	if (ioctl(fFd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &exported) == 0
			&& exported.fd >= 0) {
		struct pollfd wait = { exported.fd, POLLIN, 0 };
		poll(&wait, 1, 100);
		close(exported.fd);
	}

	// Cache coherency; also waits where the export is unsupported.
	struct dma_buf_sync sync = { DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ };
	ioctl(fFd, DMA_BUF_IOCTL_SYNC, &sync);
	fWait = system_time() - start;
#else
	(void)bitmap;
#endif
}


bool
SwapChainCpuRead::Ready(ServerBitmap* bitmap)
{
#ifdef HAVE_GBM
	GbmMemory* memory = bitmap != NULL ? bitmap->GbmBuffer() : NULL;
	if (memory == NULL)
		return true;

	const int fd = memory->ExportFd();
	if (fd < 0)
		return true;

	bool ready = true;
	struct dma_buf_export_sync_file exported;
	exported.flags = DMA_BUF_SYNC_READ;
	exported.fd = -1;
	if (ioctl(fd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &exported) == 0
			&& exported.fd >= 0) {
		struct pollfd wait = { exported.fd, POLLIN, 0 };
		ready = poll(&wait, 1, 0) != 0;
		close(exported.fd);
	}
	close(fd);
	return ready;
#else
	(void)bitmap;
	return true;
#endif
}


SwapChainCpuRead::~SwapChainCpuRead()
{
#ifdef HAVE_GBM
	if (fFd < 0)
		return;

	struct dma_buf_sync sync = { DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ };
	ioctl(fFd, DMA_BUF_IOCTL_SYNC, &sync);
	close(fFd);
#endif
}


// At most one line a second.
static void
note_plane(const char* what)
{
	static bigtime_t last = 0;
	const bigtime_t now = system_time();
	if (now - last < 1000000)
		return;

	last = now;
	fprintf(stderr, "swap chain: plane released, %s\n", what);
}


struct ViewSwapChain::Pending {
	uint64			serial;
	Generation*		generation;
	int32			index;
};


// Rows of a possibly write-combined mapping; non-temporal loads are the
// fast read there.
#ifdef SWAP_CHAIN_X86
__attribute__((target("sse4.1")))
static void
stream_row(uint8* dst, const uint8* src, size_t bytes)
{
	while (bytes > 0 && ((addr_t)src & 15) != 0) {
		*dst++ = *src++;
		bytes--;
	}
	while (bytes >= 64) {
		__m128i a = _mm_stream_load_si128((__m128i*)(src));
		__m128i b = _mm_stream_load_si128((__m128i*)(src + 16));
		__m128i c = _mm_stream_load_si128((__m128i*)(src + 32));
		__m128i d = _mm_stream_load_si128((__m128i*)(src + 48));
		_mm_storeu_si128((__m128i*)dst, a);
		_mm_storeu_si128((__m128i*)(dst + 16), b);
		_mm_storeu_si128((__m128i*)(dst + 32), c);
		_mm_storeu_si128((__m128i*)(dst + 48), d);
		src += 64;
		dst += 64;
		bytes -= 64;
	}
	memcpy(dst, src, bytes);
}
#endif


static void
copy_rows(uint8* dst, int32 dstStride, const uint8* src, int32 srcStride,
	size_t rowBytes, int32 rows)
{
#ifdef SWAP_CHAIN_X86
	static const bool stream = __builtin_cpu_supports("sse4.1");
	if (stream) {
		for (int32 y = 0; y < rows; y++)
			stream_row(dst + (size_t)y * dstStride,
				src + (size_t)y * srcStride, rowBytes);
		return;
	}
#endif
	for (int32 y = 0; y < rows; y++)
		memcpy(dst + (size_t)y * dstStride, src + (size_t)y * srcStride,
			rowBytes);
}


// Reads a rect of the dma-buf into the shadow.
static void
read_rect(ServerBitmap* from, ServerBitmap* to, int32 left, int32 top,
	int32 right, int32 bottom, bool gbmRead)
{
	const size_t bytes = (size_t)(right - left + 1) * 4;
	const int32 rows = bottom - top + 1;
	uint8* dst = to->Bits() + (size_t)top * to->BytesPerRow() + left * 4;
#ifdef HAVE_GBM
	GbmMemory* memory = from->GbmBuffer();
	if (gbmRead && memory != NULL && memory->BufferObject() != NULL) {
		uint32 stride = 0;
		void* mapData = NULL;
		void* map = gbm_bo_map(memory->BufferObject(), left, top,
			right - left + 1, rows, GBM_BO_TRANSFER_READ, &stride, &mapData);
		if (map != NULL) {
			for (int32 y = 0; y < rows; y++)
				memcpy(dst + (size_t)y * to->BytesPerRow(),
					(uint8*)map + (size_t)y * stride, bytes);
			gbm_bo_unmap(memory->BufferObject(), mapData);
			return;
		}
	}
#else
	(void)gbmRead;
#endif
	copy_rows(dst, to->BytesPerRow(),
		from->Bits() + (size_t)top * from->BytesPerRow() + left * 4,
		from->BytesPerRow(), bytes, rows);
}


// Draws the buffer through the server painter, like any bitmap.
class CpuConsumer : public SwapChainConsumer {
public:
	virtual bool Show(SwapChainFrame& frame)
	{
		if (frame.engine == NULL || frame.bitmap == NULL
				|| frame.clipping == NULL)
			return false;

		const int32 width = frame.bitmap->Width();
		const int32 height = frame.bitmap->Height();
		const BRect bounds(0, 0, width - 1, height - 1);

		ServerBitmap* source = frame.source != NULL
			? frame.source : frame.bitmap;
		SwapChainCpuRead reading(frame.source != NULL ? NULL : frame.bitmap);
		if (frame.source == NULL)
			frame.waitTime = reading.WaitTime();
		if (!frame.engineLocked)
			frame.engine->LockParallelAccess();
		const bigtime_t copyStart = system_time();
		frame.engine->ConstrainClippingRegion(frame.clipping);
		drawing_mode oldMode;
		frame.engine->SetDrawingMode(B_OP_COPY, oldMode);

		const int32 count = frame.damage != NULL
			? frame.damage->CountRects() : 0;
		if (count == 0) {
			frame.engine->DrawBitmap(source, bounds,
				frame.destination, 0);
		} else {
			for (int32 i = 0; i < count; i++) {
				BRect area = frame.damage->RectAt(i) & bounds;
				if (!area.IsValid())
					continue;
				BRect destination = area;
				destination.OffsetBy(frame.destination.left,
					frame.destination.top);
				frame.engine->DrawBitmap(source, area, destination, 0);
			}
		}

		frame.engine->SetDrawingMode(oldMode);
		if (!frame.engineLocked)
			frame.engine->UnlockParallelAccess();
		frame.copyTime = system_time() - copyStart;
		return true;
	}

	virtual void Hide()
	{
	}
};


// Scans the buffers out on an overlay plane, no copy at all. The plane
// belongs to the output the desktop reports; a buffer that is not wholly
// visible there is left to the painter.
class PlaneConsumer : public SwapChainConsumer, public OverlayPlaneListener {
public:
	PlaneConsumer(ViewSwapChain* chain)
		:
		fChain(chain),
		fPlane(NULL),
		fHardware(NULL),
		fToken(0),
		fRefused(0),
		fLost(false)
	{
	}

	virtual ~PlaneConsumer()
	{
		Release(UINT64_MAX, "destroyed");
	}

	bool Active()
	{
		_CheckLost();
		return fPlane != NULL;
	}

	int32 Token() const
	{
		return fToken;
	}

	BRect Destination() const
	{
		return fDestination;
	}

	BRect OutputFrame() const
	{
		return fOutputFrame;
	}

	virtual bool Show(SwapChainFrame& frame)
	{
		_CheckLost();

		const BRect output = frame.outputFrame;
		if (frame.hardware == NULL || !frame.fullyVisible || !output.IsValid()
				|| !output.Contains(frame.destination)
				|| frame.count < 1) {
			Release(frame.serial, !frame.fullyVisible ? "not visible"
				: "geometry");
			return false;
		}

		// Another output needs a plane of its own.
		if (fPlane != NULL && fOutputFrame != output)
			Release(frame.serial, "moved to another output");

		// A resized chain takes a second plane first, so the screen is
		// never left without the picture between the two.
		OverlayPlane* previous = NULL;
		if (fPlane != NULL && fToken != frame.token) {
			previous = fPlane;
			const int32 previousToken = fToken;
			fPlane = NULL;
			fToken = 0;
			if (fRefused != frame.token && _Claim(frame)) {
				fHardware->ReleaseOverlayPlane(previous);
				previous = NULL;
				fChain->NotifyReleased(frame.serial);
			} else {
				fPlane = previous;
				fToken = previousToken;
				Release(frame.serial, "resized, no second plane");
			}
		}
		if (fPlane == NULL) {
			if (fRefused == frame.token || !_Claim(frame)) {
				fRefused = frame.token;
				return false;
			}
		}

		const BRect source(0, 0, frame.bitmap->Width() - 1,
			frame.bitmap->Height() - 1);
		if (fPlane->Show(frame.index, frame.serial, source, frame.destination)
				!= B_OK) {
			fRefused = frame.token;
			Release(frame.serial, "plane refused the buffer");
			return false;
		}

		fDestination = frame.destination;
		fSource = source;
		frame.async = true;
		return true;
	}

	virtual void Hide()
	{
		Release(UINT64_MAX, "hidden");
	}

	// A moved view keeps its plane: only the position changes.
	bool Follow(const BRect& destination)
	{
		_CheckLost();
		if (fPlane == NULL || fSource.Width() < 0)
			return false;

		const BRect source = fSource;
		if (fPlane->Configure(source, destination) != B_OK)
			return false;

		fDestination = destination;
		return true;
	}

	// Gives the plane back. Updates staged before keepFrom are dead.
	void Release(uint64 keepFrom, const char* reason)
	{
		_CheckLost();
		if (fPlane == NULL)
			return;

		note_plane(reason);
		OverlayPlane* plane = fPlane;
		fPlane = NULL;
		fToken = 0;
		fHardware->ReleaseOverlayPlane(plane);
		fChain->NotifyReleased(keepFrom);
	}

	virtual void PlaneShown(uint64 serial, bigtime_t when)
	{
		fChain->NotifyShown(serial, when);
	}

	virtual void PlaneDropped(uint64 serial)
	{
		fChain->NotifyDropped(serial);
	}

	virtual void PlaneLost()
	{
		fLost = true;
		fChain->NotifyLost(fDestination);
	}

private:
	void _CheckLost()
	{
		// The hardware deleted the plane; there is nothing to give back.
		if (fLost) {
			fPlane = NULL;
			fToken = 0;
			fLost = false;
		}
	}

	bool _Claim(const SwapChainFrame& frame)
	{
#ifndef HAVE_GBM
		return false;
#else
		OverlayPlane* plane = NULL;
		if (frame.hardware->AcquireOverlayPlaneForBitmap(frame.buffers[0],
				&plane, frame.destination) != B_OK)
			return false;

		for (int32 i = 1; i < frame.count; i++) {
			GbmMemory* memory = frame.buffers[i]->GbmBuffer();
			int fd = memory != NULL ? memory->ExportFd() : -1;
			int32 index = -1;
			status_t status = fd >= 0
				? plane->AddBuffer(fd, memory->Width(), memory->Height(),
					memory->Format(), memory->Stride(), memory->Handle(),
					&index)
				: B_ERROR;
			if (fd >= 0)
				close(fd);
			if (status != B_OK || index != i) {
				frame.hardware->ReleaseOverlayPlane(plane);
				return false;
			}
		}

		plane->SetListener(this);
		fPlane = plane;
		fHardware = frame.hardware;
		fToken = frame.token;
		fOutputFrame = frame.outputFrame;
		return true;
#endif
	}

	ViewSwapChain*	fChain;
	OverlayPlane*	fPlane;
	::HWInterface*	fHardware;
	int32			fToken;
	int32			fRefused;
	BRect			fDestination;
	BRect			fSource;
	BRect			fOutputFrame;
	volatile bool	fLost;
};


ViewSwapChain::ViewSwapChain(ServerApp* app, ServerWindow* window, View* view)
	:
	fApp(app),
	fWindow(window),
	fView(view),
	fOwnerWindow(NULL),
	fLock("view swap chain"),
	fCurrent(NULL),
	fShownGeneration(NULL),
	fShownIndex(-1),
	fCpu(NULL),
	fPlane(NULL),
	fSerial(0),
	fPeriod(0),
	fPeriodAt(0),
	fCounted(false),
	fStats(false),
	fStatsStart(0),
	fLastShown(0),
	fWorstGap(0),
	fPresented(0),
	fShown(0),
	fReplaced(0),
	fCopyTime(0),
	fWaitTime(0),
	fReadTime(0),
	fGpuTime(0),
	fDeferSince(0),
	fCopies(0),
	fPump(-1),
	fPumpWake(false),
	fPumpQuit(false),
	fMailSerial(0),
	fMailGeneration(NULL),
	fMailWhole(false),
	fShadowLock("swap chain shadow"),
	fShadowValid(false),
	fGbmRead(getenv("VOS_SWAP_CHAIN_GBM_READ") != NULL
		&& atoi(getenv("VOS_SWAP_CHAIN_GBM_READ")) != 0),
	fGpu(NULL)
{
	const char* stats = getenv("VOS_SWAP_CHAIN_STATS");
	fStats = stats != NULL && atoi(stats) != 0;
}


ViewSwapChain::~ViewSwapChain()
{
	delete fGpu;
	// The pump first: it posts to the window.
	if (fPump >= 0) {
		{
			std::lock_guard<std::mutex> guard(fPumpMutex);
			fPumpQuit = true;
		}
		fPumpCondition.notify_all();
		status_t result;
		wait_for_thread(fPump, &result);
	}

	// The plane first: nothing may be reported once the buffers go.
	Hide(false, "chain deleted");

	::Window* owner = fOwnerWindow;
	if (owner != NULL)
		owner->RemoveSwapChain(this);

	BAutolock locker(fLock);
	fWindow = NULL;
	fView = NULL;
	fOwnerWindow = NULL;
	for (size_t i = 0; i < fGenerations.size(); i++) {
		fApp->ReleaseSwapChain(fGenerations[i]->bytes, false);
		delete fGenerations[i];
	}
	fGenerations.clear();
	if (fCounted)
		fApp->ReleaseSwapChain(0, true);

	PlaneConsumer* plane = fPlane;
	fPlane = NULL;
	delete fCpu;
	locker.Unlock();
	delete plane;
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

	// Where the view lies picks the card that allocates.
	BRect where = _DestinationFor(width, height);

	// Only a chain a plane can take needs scanout memory; the CPU copy
	// reads the rest, best kept in system memory.
	const bool planeCapable = hardware->HasOverlayPlane(where);

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
			B_BITMAP_SUPPORTS_GBM, -1, B_MAIN_SCREEN_ID.id, NULL,
			where.IsValid() ? &where : NULL, planeCapable);
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
	if (!resize) {
		fCounted = true;
		fOwnerWindow = fView->Window();
	}
	_Prune();
	locker.Unlock();

	if (!resize && fOwnerWindow != NULL)
		fOwnerWindow->AddSwapChain(this);
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
	::HWInterface* hardware = fWindow->Desktop()->HWInterface();
	_UpdatePeriod(hardware);

	{
		BAutolock locker(fLock);
		generation = _Find(token);
		if (generation == NULL || index < 0 || index >= generation->count
				|| generation->held[index]) {
			fprintf(stderr, "swap chain: present of buffer %d rejected\n",
				(int)index);
			return;
		}

		// Named before anything can report on it.
		Pending pending = { ++fSerial, generation, index };
		try {
			fPending.push_back(pending);
		} catch (std::bad_alloc&) {
			_Retire(generation, index, system_time());
			return;
		}

		generation->held[index] = true;
		generation->presented = true;
		if (fStats) {
			fPresented++;
			_Report(system_time());
		}
		frame.bitmap = generation->buffers[index].Get();
		frame.token = token;
		frame.index = index;
		frame.destination = _Destination(generation);
		frame.damage = &damage;
		frame.engine = engine;
		frame.flags = flags;
		frame.hardware = hardware;
		frame.count = generation->count;
		for (int32 i = 0; i < generation->count; i++)
			frame.buffers[i] = generation->buffers[i].Get();
		frame.serial = pending.serial;

		if (fCpu == NULL)
			fCpu = new(std::nothrow) CpuConsumer;
		if (fPlane == NULL && hardware != NULL)
			fPlane = new(std::nothrow) PlaneConsumer(this);
	}

	// Same buffer geometry on the same plane and nothing changed under
	// the view: what the plane was given still holds.
	BRegion lazyClipping;
	PlaneConsumer* plane = fPlane;
	const bool steady = clipping == NULL && plane != NULL && plane->Active()
		&& plane->Token() == token && plane->Destination() == frame.destination;
	if (steady) {
		frame.outputFrame = plane->OutputFrame();
		frame.fullyVisible = true;
	} else {
		if (clipping == NULL && fOwnerWindow != NULL)
			fOwnerWindow->GetEffectiveDrawingRegion(fView, lazyClipping);
		if (hardware != NULL) {
			frame.outputFrame
				= hardware->OverlayOutputFrame(frame.destination);
			frame.fullyVisible = fOwnerWindow != NULL
				&& fOwnerWindow->IsRectFullyVisible(fView, frame.destination);
		}
	}
	frame.clipping = clipping != NULL ? clipping : &lazyClipping;

	// Neither consumer runs under our lock: both take others.
	bool shown = false;
	if (fPlane != NULL)
		shown = fPlane->Show(frame);
	if (!shown && fCpu != NULL) {
		// Newest frame wins; the copy waits for the next refresh.
		if (_Post(frame, generation)) {
			shown = true;
			frame.async = true;
		} else {
			// The plane refused a steady present: the painter needs its
			// region.
			if (clipping == NULL && steady && fOwnerWindow != NULL)
				fOwnerWindow->GetEffectiveDrawingRegion(fView, lazyClipping);
			{
				BAutolock shadowLocker(fShadowLock);
				fShadowValid = false;
			}
			shown = fCpu->Show(frame);
			_CountCopy(frame.waitTime, frame.readTime, frame.copyTime, 0);
		}
	}

	BAutolock locker(fLock);
	if (shown && frame.async) {
		// The plane tells when it is on screen; the mailbox, when flushed.
		if (fMailSerial != 0 && fMailSerial != frame.serial)
			_DropMailbox();
		_Prune();
		return;
	}

	for (size_t i = 0; i < fPending.size(); i++) {
		if (fPending[i].serial == frame.serial) {
			fPending.erase(fPending.begin() + i);
			break;
		}
	}

	if (shown) {
		_CountShown(system_time());
		Generation* previous = fShownGeneration;
		const int32 previousIndex = fShownIndex;
		fShownGeneration = generation;
		fShownIndex = index;
		if (previous != NULL)
			_Retire(previous, previousIndex, system_time());
	} else {
		// Nothing was shown, so nothing stays held.
		_Retire(generation, index, system_time());
	}
	_Prune();
}


// Records the newest frame for the pump; the one it replaces is dropped,
// its damage carried over.
bool
ViewSwapChain::_Post(const SwapChainFrame& frame, Generation* generation)
{
	BAutolock locker(fLock);
	if (fOwnerWindow == NULL || fWindow == NULL || frame.engine == NULL)
		return false;

	if (fPump < 0) {
		fPump = spawn_thread(_PumpEntry, "swap chain pump",
			B_DISPLAY_PRIORITY, this);
		if (fPump < 0)
			return false;
		resume_thread(fPump);
	}

	bool whole = frame.damage == NULL || frame.damage->CountRects() == 0;
	BRegion damage;
	if (!whole)
		damage = *frame.damage;
	if (fMailSerial != 0) {
		if (fMailWhole || fMailGeneration != generation)
			whole = true;
		else if (!whole)
			damage.Include(&fMailDamage);
		_DropMailbox();
	}

	fMailSerial = frame.serial;
	fMailGeneration = generation;
	fMailWhole = whole;
	fMailDamage = damage;
	locker.Unlock();

	{
		std::lock_guard<std::mutex> guard(fPumpMutex);
		fPumpWake = true;
	}
	fPumpCondition.notify_one();
	return true;
}


// fLock held.
void
ViewSwapChain::_DropMailbox()
{
	for (size_t i = 0; i < fPending.size(); i++) {
		if (fPending[i].serial != fMailSerial)
			continue;

		_CountReplaced();
		_Retire(fPending[i].generation, fPending[i].index, system_time());
		fPending.erase(fPending.begin() + i);
		break;
	}
	_ResetMailbox();
}


// fLock held.
void
ViewSwapChain::_ResetMailbox()
{
	fMailSerial = 0;
	fMailGeneration = NULL;
	fMailWhole = false;
	fMailDamage.MakeEmpty();
}


int32
ViewSwapChain::_PumpEntry(void* self)
{
	((ViewSwapChain*)self)->_Pump();
	return 0;
}


// At most one flush per refresh while a frame waits.
void
ViewSwapChain::_Pump()
{
	bigtime_t next = 0;
	std::unique_lock<std::mutex> guard(fPumpMutex);
	while (true) {
		fPumpCondition.wait(guard, [this] { return fPumpWake || fPumpQuit; });
		if (fPumpQuit)
			return;
		fPumpWake = false;
		guard.unlock();

		while (true) {
			const bigtime_t now = system_time();
			if (next > now)
				snooze(next - now);

			bool quit;
			{
				std::lock_guard<std::mutex> quitGuard(fPumpMutex);
				quit = fPumpQuit;
			}
			BAutolock locker(fLock);
			if (quit || fMailSerial == 0)
				break;

			fWindow->PostMessage(AS_INTERNAL_FLUSH_SWAP_CHAINS, 0);
			next = system_time() + (fPeriod > 0 ? fPeriod : 16667);
		}
		guard.lock();
	}
}


// The window thread, drawing as for Present.
void
ViewSwapChain::Flush(DrawingEngine* engine)
{
	BReference<ServerBitmap> bitmap;
	BRegion damage;
	BRect destination;
	::Window* owner;

	// Never wait on the GPU here: keep the shown frame and let the pump
	// retry, unless the fence stays late for two periods.
	{
		BReference<ServerBitmap> next;
		{
			BAutolock locker(fLock);
			if (fMailSerial == 0)
				return;

			for (size_t i = 0; i < fPending.size(); i++) {
				if (fPending[i].serial == fMailSerial) {
					next = fPending[i].generation->buffers[fPending[i].index];
					break;
				}
			}
		}
		if (next.Get() != NULL && !SwapChainCpuRead::Ready(next.Get())) {
			const bigtime_t now = system_time();
			BAutolock locker(fLock);
			if (fDeferSince == 0)
				fDeferSince = now;
			if (now - fDeferSince < 2 * (fPeriod > 0 ? fPeriod : 16667))
				return;
		}
	}

	{
		BAutolock locker(fLock);
		fDeferSince = 0;
		if (fMailSerial == 0)
			return;

		size_t i = 0;
		while (i < fPending.size() && fPending[i].serial != fMailSerial)
			i++;
		if (i == fPending.size()) {
			_ResetMailbox();
			return;
		}

		Generation* generation = fPending[i].generation;
		const int32 index = fPending[i].index;
		fPending.erase(fPending.begin() + i);
		if (!fMailWhole)
			damage = fMailDamage;
		_ResetMailbox();

		const bigtime_t now = system_time();
		_CountShown(now);
		Generation* previous = fShownGeneration;
		const int32 previousIndex = fShownIndex;
		fShownGeneration = generation;
		fShownIndex = index;
		if (previous != NULL)
			_Retire(previous, previousIndex, now);

		bitmap = generation->buffers[index];
		destination = _Destination(generation);
		owner = fOwnerWindow;
		_Prune();
	}

	if (owner == NULL || engine == NULL || fCpu == NULL
			|| !destination.IsValid() || !fView->IsVisible())
		return;

	BRegion clipping;
	owner->GetEffectiveDrawingRegion(fView, clipping);

	SwapChainFrame frame;
	frame.bitmap = bitmap.Get();
	frame.destination = destination;
	frame.damage = &damage;
	frame.engine = engine;
	frame.clipping = &clipping;

	BReference<ServerBitmap> shadow;
	bigtime_t gpuTime = 0;
	{
		const bigtime_t readStart = system_time();
		SwapChainGpuCopy* gpu = NULL;
#ifdef HAVE_GBM
		::HWInterface* hardware = fWindow->Desktop()->HWInterface();
		if (hardware != NULL && fGpu == NULL && SwapChainGpuCopy::Enabled())
			fGpu = new(std::nothrow) SwapChainGpuCopy();
		if (fGpu != NULL && !fGpu->Failed()
				&& fGpu->Copy(hardware->GbmDeviceFor(destination),
					bitmap.Get())) {
			gpu = fGpu;
			gpuTime = fGpu->Time();
		}
#endif
		if (gpu != NULL) {
			shadow = _FillShadow(bitmap.Get(), damage, gpu);
		} else {
			SwapChainCpuRead reading(bitmap.Get());
			frame.waitTime = reading.WaitTime();
			shadow = _FillShadow(bitmap.Get(), damage, NULL);
		}
		frame.readTime = system_time() - readStart;
	}
	frame.source = shadow.Get();
	fCpu->Show(frame);
	_CountCopy(frame.waitTime, frame.readTime, frame.copyTime, gpuTime);
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
	if (fPlane != NULL && fPlane->Active())
		return;

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
	frame.index = index;
	frame.destination = destination;
	frame.engine = engine;
	frame.engineLocked = true;
	frame.clipping = &region;
	BReference<ServerBitmap> shadow;
	{
		BAutolock shadowLocker(fShadowLock);
		if (fShadowValid && fShadow.Get() != NULL
				&& fShadow->Width() == bitmap->Width()
				&& fShadow->Height() == bitmap->Height())
			shadow = fShadow;
	}
	frame.source = shadow.Get();
	CpuConsumer().Show(frame);
}


void
ViewSwapChain::Compose(ServerBitmap* target, const BRect& bounds)
{
	if (fPlane == NULL || !fPlane->Active() || fOwnerWindow == NULL
			|| fView == NULL)
		return;

	BReference<ServerBitmap> bitmap;
	BRect destination;
	{
		BAutolock locker(fLock);
		if (fShownGeneration == NULL || fShownIndex < 0)
			return;

		bitmap = fShownGeneration->buffers[fShownIndex];
		destination = _Destination(fShownGeneration);
	}

	BRegion visible;
	fOwnerWindow->VisibleViewRegion(fView, visible);
	BRegion area(destination & bounds);
	visible.IntersectWith(&area);

	SwapChainCpuRead reading(bitmap.Get());
	for (int32 i = 0; i < visible.CountRects(); i++) {
		const BRect rect = visible.RectAt(i);
		target->ImportBits(bitmap->Bits(), bitmap->BitsLength(),
			bitmap->BytesPerRow(), bitmap->ColorSpace(),
			BPoint(rect.left - destination.left, rect.top - destination.top),
			BPoint(rect.left - bounds.left, rect.top - bounds.top),
			rect.IntegerWidth() + 1, rect.IntegerHeight() + 1);
	}
}


void
ViewSwapChain::Hide(bool redraw, const char* reason)
{
	PlaneConsumer* plane = fPlane;
	if (plane == NULL || !plane->Active())
		return;

	const BRect destination = plane->Destination();
	plane->Release(UINT64_MAX, reason);

	::Window* owner = fOwnerWindow;
	if (redraw && owner != NULL) {
		BRegion dirty(destination);
		owner->MarkContentDirtyAsync(dirty);
	}
}


void
ViewSwapChain::Revalidate()
{
	PlaneConsumer* plane = fPlane;
	if (plane == NULL || !plane->Active() || fOwnerWindow == NULL)
		return;

	BRect destination;
	{
		BAutolock locker(fLock);
		Generation* generation = _Find(plane->Token());
		if (generation == NULL || fView == NULL) {
			destination = BRect();
		} else
			destination = _Destination(generation);
	}

	::HWInterface* hardware = fWindow->Desktop()->HWInterface();
	const char* reason = NULL;
	if (hardware == NULL || !destination.IsValid())
		reason = "no destination";
	else if (!hardware->OverlayOutputFrame(destination).Contains(destination)
			|| hardware->OverlayOutputFrame(destination) != plane->OutputFrame())
		reason = "off output";
	else if (!fOwnerWindow->IsRectFullyVisible(fView, destination))
		reason = "covered";
	else if (destination != plane->Destination()
			&& !plane->Follow(destination))
		reason = "moved";

	if (reason != NULL)
		Hide(true, reason);
}


void
ViewSwapChain::Detach()
{
	BAutolock locker(fLock);
	fOwnerWindow = NULL;
}


void
ViewSwapChain::NotifyShown(uint64 serial, bigtime_t when)
{
	{
		BAutolock locker(fLock);
		for (size_t i = 0; i < fPending.size(); i++) {
			if (fPending[i].serial != serial)
				continue;

			_CountShown(when);
			Generation* previous = fShownGeneration;
			const int32 previousIndex = fShownIndex;
			fShownGeneration = fPending[i].generation;
			fShownIndex = fPending[i].index;
			fPending.erase(fPending.begin() + i);
			if (previous != NULL)
				_Retire(previous, previousIndex, when);
			break;
		}
		_Prune();
	}
}


void
ViewSwapChain::NotifyDropped(uint64 serial)
{
	{
		BAutolock locker(fLock);
		for (size_t i = 0; i < fPending.size(); i++) {
			if (fPending[i].serial != serial)
				continue;

			_CountReplaced();
			_Retire(fPending[i].generation, fPending[i].index, system_time());
			if (serial == fMailSerial)
				_ResetMailbox();
			fPending.erase(fPending.begin() + i);
			break;
		}
		_Prune();
	}
}


void
ViewSwapChain::NotifyLost(const BRect& area)
{
	NotifyReleased(UINT64_MAX);

	// Back to the painter: the area is drawn again.
	::Window* owner = fOwnerWindow;
	if (owner != NULL && area.IsValid()) {
		BRegion dirty(area);
		owner->MarkContentDirtyAsync(dirty);
	}
}


void
ViewSwapChain::NotifyReleased(uint64 serial)
{
	{
		BAutolock locker(fLock);
		_DropPending(serial);
		_Prune();
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

	const int32 period = fPeriod;
	swap_chain_state* state = generation->state;
	state->releaseTime = when;
	state->refreshPeriod = period;
	__atomic_store_n(&state->buffer[index], (int32)kSwapBufferFree,
		__ATOMIC_RELEASE);

}


// Retires every staged buffer older than serial. fLock held.
void
ViewSwapChain::_DropPending(uint64 below)
{
	for (size_t i = fPending.size(); i-- > 0;) {
		if (fPending[i].serial >= below)
			continue;

		_CountReplaced();
		_Retire(fPending[i].generation, fPending[i].index, system_time());
		if (fPending[i].serial == fMailSerial)
			_ResetMailbox();
		fPending.erase(fPending.begin() + i);
	}
}


// fLock held.
void
ViewSwapChain::_CountShown(bigtime_t when)
{
	if (!fStats)
		return;

	fShown++;
	if (fLastShown != 0 && when - fLastShown > fWorstGap)
		fWorstGap = when - fLastShown;
	fLastShown = when;
	_Report(system_time());
}


// Fills the back shadow, then publishes it; the shown one is never written.
BReference<ServerBitmap>
ViewSwapChain::_FillShadow(ServerBitmap* bitmap, const BRegion& damage,
	SwapChainGpuCopy* gpu)
{
	BReference<ServerBitmap> shown;
	BReference<ServerBitmap> back;
	bool valid;
	{
		BAutolock locker(fShadowLock);
		valid = fShadowValid;
		shown = fShadow;
		back.SetTo(fSpare.Get());
		fSpare.Unset();
	}

	if (bitmap == NULL || bitmap->Bits() == NULL
			|| bitmap->ColorSpace() != B_RGB32) {
		BAutolock locker(fShadowLock);
		fShadowValid = false;
		return BReference<ServerBitmap>();
	}

	if (shown.Get() != NULL && (shown->Width() != bitmap->Width()
			|| shown->Height() != bitmap->Height()))
		valid = false;

	// A reader may still hold it.
	if (back.Get() == NULL || back->CountReferences() > 1
			|| back->Width() != bitmap->Width()
			|| back->Height() != bitmap->Height()) {
		back.SetTo(new(std::nothrow) UtilityBitmap(bitmap->Bounds(),
			B_RGB32, 0), true);
		if (back.Get() == NULL || !back->IsValid()
				|| back->BytesPerRow() < bitmap->Width() * 4) {
			BAutolock locker(fShadowLock);
			fShadowValid = false;
			return BReference<ServerBitmap>();
		}
	}

	const bool whole = !valid || damage.CountRects() == 0;
	const BRect bounds = bitmap->Bounds();
	// The rect from the GPU copy or straight from the buffer.
	auto read = [&](int32 left, int32 top, int32 right, int32 bottom) {
		if (gpu != NULL) {
			copy_rows(back->Bits() + (size_t)top * back->BytesPerRow()
					+ left * 4, back->BytesPerRow(),
				gpu->Bits() + (size_t)top * gpu->Stride() + left * 4,
				gpu->Stride(), (size_t)(right - left + 1) * 4,
				bottom - top + 1);
		} else
			read_rect(bitmap, back.Get(), left, top, right, bottom, fGbmRead);
	};
	if (whole) {
		read(0, 0, bitmap->Width() - 1, bitmap->Height() - 1);
	} else {
		// The back one is a frame behind: start from the shown one.
		memcpy(back->Bits(), shown->Bits(),
			(size_t)back->BytesPerRow() * back->Height());
		for (int32 i = 0; i < damage.CountRects(); i++) {
			const BRect rect = damage.RectAt(i) & bounds;
			if (!rect.IsValid())
				continue;
			read((int32)rect.left, (int32)rect.top, (int32)rect.right,
				(int32)rect.bottom);
		}
	}

	BAutolock locker(fShadowLock);
	fSpare = fShadow;
	fShadow = back;
	fShadowValid = true;
	return back;
}


void
ViewSwapChain::_CountCopy(bigtime_t wait, bigtime_t read, bigtime_t time,
	bigtime_t gpu)
{
	if (!fStats || time <= 0)
		return;

	atomic_add64(&fReadTime, read);
	atomic_add64(&fGpuTime, gpu);
	atomic_add64(&fWaitTime, wait);
	atomic_add64(&fCopyTime, time);
	atomic_add(&fCopies, 1);
}


// fLock held.
void
ViewSwapChain::_CountReplaced()
{
	if (fStats)
		fReplaced++;
}


// At most one line in two seconds. fLock held.
void
ViewSwapChain::_Report(bigtime_t now)
{
	if (fStatsStart == 0) {
		fStatsStart = now;
		return;
	}

	const bigtime_t span = now - fStatsStart;
	if (span < 2000000)
		return;

	const int64 copyTime = atomic_get_and_set64(&fCopyTime, 0);
	const int64 waitTime = atomic_get_and_set64(&fWaitTime, 0);
	const int64 readTime = atomic_get_and_set64(&fReadTime, 0);
	const int64 gpuTime = atomic_get_and_set64(&fGpuTime, 0);
	const int32 copies = atomic_get_and_set(&fCopies, 0);
	fprintf(stderr, "swap chain %s: shown %llu/s, presented %llu/s, "
		"replaced %u, worst gap %.1f ms, wait %lld us, read %lld us, copy %lld us, gpu %lld us\n",
		fView != NULL ? fView->Name() : "?",
		(unsigned long long)fShown * 1000000 / span,
		(unsigned long long)fPresented * 1000000 / span, (unsigned)fReplaced,
		fWorstGap / 1000.0,
		(long long)(copies > 0 ? waitTime / copies : 0),
		(long long)(copies > 0 ? readTime / copies : 0),
		(long long)(copies > 0 ? copyTime / copies : 0),
		(long long)(copies > 0 ? gpuTime / copies : 0));
	fStatsStart = now;
	fShown = fPresented = fReplaced = 0;
	fWorstGap = 0;
}


void
ViewSwapChain::_UpdatePeriod(::HWInterface* hardware)
{
	if (hardware == NULL)
		return;

	// The mode changes rarely.
	const bigtime_t now = system_time();
	if (fPeriod != 0 && now - fPeriodAt < 1000000)
		return;
	fPeriodAt = now;

	display_mode mode;
	hardware->GetMode(&mode);
	if (mode.timing.pixel_clock == 0)
		return;

	const uint64 pixels = (uint64)mode.timing.h_total * mode.timing.v_total;
	fPeriod = (int32)(pixels * 1000 / mode.timing.pixel_clock);
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
	return _DestinationFor(generation->width, generation->height);
}


BRect
ViewSwapChain::_DestinationFor(int32 width, int32 height) const
{
	if (fView == NULL)
		return BRect();

	const IntRect bounds = fView->Bounds();
	BRect rect(bounds.left, bounds.top, bounds.left + width - 1,
		bounds.top + height - 1);
	fView->LocalToScreenTransform().Apply(&rect);
	return rect;
}
