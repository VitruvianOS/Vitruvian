/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "ViewSwapChain.h"

#include <algorithm>

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <atomic>
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

#include <BufferAllocator.h>
#include <PortFds.h>

#include "BitmapManager.h"
#include "Desktop.h"
#include "DrawingEngine.h"
#include "HWInterface.h"
#include "OverlayPlane.h"
#include "ServerApp.h"
#include "ServerBitmap.h"
#include "ServerWindow.h"
#include "DisplayProfile.h"
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
	// Buffers the client imported, shown without a copy.
	bool					external;
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
		external(false),
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


// Waits for the fence the buffer's last writer left on its dma-buf.
static void
wait_for_writer(ServerBitmap* bitmap)
{
#ifdef HAVE_GBM
	GbmMemory* memory = bitmap != NULL ? bitmap->GbmBuffer() : NULL;
	const int fd = memory != NULL ? memory->ExportFd() : -1;
	if (fd < 0)
		return;

	struct dma_buf_export_sync_file exported;
	exported.flags = DMA_BUF_SYNC_READ;
	exported.fd = -1;
	if (ioctl(fd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &exported) == 0
			&& exported.fd >= 0) {
		struct pollfd wait = { exported.fd, POLLIN, 0 };
		poll(&wait, 1, 100);
		close(exported.fd);
	}
	close(fd);
#else
	(void)bitmap;
#endif
}


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
note_plane(bool scanout, const char* what)
{
	static bigtime_t last = 0;
	const bigtime_t now = system_time();
	if (now - last < 1000000)
		return;

	last = now;
	if (scanout)
		fprintf(stderr, "swap chain: direct scanout off (%s)\n", what);
	else
		fprintf(stderr, "swap chain: plane released, %s\n", what);
}


// Reads the pump may take; the trials settle on one of the others.
enum {
	kTrialGpu = 0,
	kTrialCpu = 1,
	kPathGpu = 2,
	kPathCpu = 3
};


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
	// The pages of a tiled buffer are not rows: only the map detiles.
	const bool pageless = from->Bits() == NULL;
	if (memory != NULL && memory->BufferObject() != NULL
			&& (gbmRead || pageless || !memory->IsLinear())) {
		uint32 stride = 0;
		void* mapData = NULL;
		void* map = BufferAllocator::Map(memory->BufferObject(), left, top,
			right - left + 1, rows, false, &stride, &mapData);
		if (map != NULL) {
			for (int32 y = 0; y < rows; y++)
				memcpy(dst + (size_t)y * to->BytesPerRow(),
					(uint8*)map + (size_t)y * stride, bytes);
			BufferAllocator::Unmap(memory->BufferObject(), mapData);
			return;
		}
	}
	if (pageless)
		return;
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
		if (source->Bits() == NULL)
			return false;
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
		fRegistered(0),
		fRefused(0),
		fScan(false),
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

	// The primary plane shows the buffers, not an overlay.
	bool Scanout() const
	{
		return fScan;
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

		// The primary plane cannot be shared between two chains.
		if (fPlane != NULL && fScan && fToken != frame.token)
			Release(frame.serial, "resized");

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

		// An imported set grows after the plane is claimed.
		if (frame.count > fRegistered
				&& !_AddBuffers(fPlane, frame, fRegistered)) {
			fRefused = frame.token;
			Release(frame.serial, "plane refused a buffer");
			return false;
		}
		fRegistered = frame.count;

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
		if (fPlane == NULL || fScan || fSource.Width() < 0)
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

		note_plane(fScan, reason);
		OverlayPlane* plane = fPlane;
		fPlane = NULL;
		fToken = 0;
		fRegistered = 0;
		if (fScan)
			fHardware->ReleaseScanout(plane);
		else
			fHardware->ReleaseOverlayPlane(plane);
		fScan = false;
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
			// A scanout is the chain's to free.
			if (fScan && fPlane != NULL) {
				note_plane(true, "display changed");
				fHardware->ReleaseScanout(fPlane);
			}
			fPlane = NULL;
			fToken = 0;
			fRegistered = 0;
			fScan = false;
			fLost = false;
		}
	}

	// Registers buffers from..count-1; the plane numbers them in order.
	bool _AddBuffers(OverlayPlane* plane, const SwapChainFrame& frame,
		int32 from)
	{
#ifndef HAVE_GBM
		return false;
#else
		for (int32 i = from; i < frame.count; i++) {
			GbmMemory* memory = frame.buffers[i]->GbmBuffer();
			int fd = memory != NULL ? memory->ExportFd() : -1;
			int32 index = -1;
			status_t status = fd >= 0
				? plane->AddBuffer(fd, memory->Width(), memory->Height(),
					memory->Format(), memory->Stride(), memory->Handle(),
					&index, memory->Modifier())
				: B_ERROR;
			if (fd >= 0)
				close(fd);
			if (status != B_OK || index != i)
				return false;
		}
		return true;
#endif
	}

	bool _Claim(const SwapChainFrame& frame)
	{
#ifndef HAVE_GBM
		return false;
#else
		OverlayPlane* plane = NULL;
		if (frame.hardware->AcquireOverlayPlaneForBitmap(frame.buffers[0],
				&plane, frame.destination) != B_OK)
			return _ClaimScanout(frame);

		if (!_AddBuffers(plane, frame, 1)) {
			frame.hardware->ReleaseOverlayPlane(plane);
			return false;
		}
		fRegistered = frame.count;

		plane->SetListener(this);
		fPlane = plane;
		fHardware = frame.hardware;
		fToken = frame.token;
		fOutputFrame = frame.outputFrame;
		fScan = false;
		return true;
#endif
	}

	// The whole output is this chain's: the primary plane takes it.
	bool _ClaimScanout(const SwapChainFrame& frame)
	{
#ifndef HAVE_GBM
		return false;
#else
		if (frame.destination != frame.outputFrame)
			return false;

		OverlayPlane* plane = NULL;
		if (frame.hardware->AcquireScanoutForBitmap(frame.buffers[0], &plane,
				frame.destination) != B_OK)
			return false;

		if (!_AddBuffers(plane, frame, 1)) {
			frame.hardware->ReleaseScanout(plane);
			return false;
		}
		fRegistered = frame.count;

		plane->SetListener(this);
		fPlane = plane;
		fHardware = frame.hardware;
		fToken = frame.token;
		fOutputFrame = frame.outputFrame;
		fScan = true;
		fprintf(stderr, "swap chain: direct scanout on (%dx%d)\n",
			(int)frame.bitmap->Width(), (int)frame.bitmap->Height());
		return true;
#endif
	}

	ViewSwapChain*	fChain;
	OverlayPlane*	fPlane;
	::HWInterface*	fHardware;
	int32			fToken;
	int32			fRegistered;
	int32			fRefused;
	bool			fScan;
	BRect			fDestination;
	BRect			fSource;
	BRect			fOutputFrame;
	volatile bool	fLost;
};


static std::mutex sChainsMutex;
static std::vector<ViewSwapChain*> sChains;
static std::atomic<bigtime_t> sFreshSince(0);


ViewSwapChain::ViewSwapChain(ServerApp* app, ServerWindow* window, View* view)
	:
	fApp(app),
	fWindow(window),
	fView(view),
	fOwnerWindow(NULL),
	fLock("view swap chain"),
	fCurrent(NULL),
	fExternal(NULL),
	fImportPort(-1),
	fExternalWarned(false),
	fShownGeneration(NULL),
	fShownIndex(-1),
	fCpu(NULL),
	fPlane(NULL),
	fSerial(0),
	fPeriod(0),
	fPeriodAt(0),
	fLead(6000),
	fLatency(0),
	fWakeAt(0),
	fDeadline(0),
	fFlushShown(false),
	fCounted(false),
	fUsage(0),
	fRequestedCount(0),
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
	fGpu(NULL),
	fPath(SwapChainGpuCopy::Mode() == 0 ? kPathCpu
		: SwapChainGpuCopy::Mode() == 1 ? kPathGpu : kTrialGpu),
	fTrialFrames(0)
{
	fTrialTime[0] = fTrialTime[1] = 0;
	{
		std::lock_guard<std::mutex> guard(sChainsMutex);
		sChains.push_back(this);
	}
	const char* stats = getenv("VOS_SWAP_CHAIN_STATS");
	fStats = stats != NULL && atoi(stats) != 0;
}


ViewSwapChain::~ViewSwapChain()
{
	{
		std::lock_guard<std::mutex> guard(sChainsMutex);
		sChains.erase(std::remove(sChains.begin(), sChains.end(), this),
			sChains.end());
	}
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
	// Only after the pump: nothing flushes through the context any more.
	delete fGpu;
	fGpu = NULL;

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
	delete fExternal;
	fExternal = NULL;
	if (fImportPort >= 0)
		delete_port(fImportPort);
	if (fCounted)
		fApp->ReleaseSwapChain(0, true);

	PlaneConsumer* plane = fPlane;
	fPlane = NULL;
	delete fCpu;
	locker.Unlock();
	delete plane;
}


void
ViewSwapChain::SetRequest(uint32 usage, const uint64* modifiers, int32 count)
{
	BAutolock locker(fLock);
	fUsage = usage;
	fRequestedCount = 0;
	for (int32 i = 0; i < count && i < 4; i++)
		fRequested[fRequestedCount++] = modifiers[i];
}


// The layouts a chain's buffers may take: what the renderer can write and
// the planes can show, within what the client takes. Empty means linear.
static int32
negotiate_modifiers(::HWInterface* hardware, const BRect& where,
	bool planeCapable, bool screenCapable, uint32 usage,
	const uint64* requested, int32 requestedCount, uint64* result)
{
	using namespace BPrivate;
#ifdef HAVE_GBM
	if ((usage & kSwapUsageGpu) == 0)
		return 0;
	// No modifier but linear asked for: nothing to negotiate.
	bool tiledAllowed = requestedCount == 0;
	for (int32 i = 0; i < requestedCount; i++)
		tiledAllowed |= requested[i] != kSwapModifierLinear;
	if (!tiledAllowed)
		return 0;

	BufferAllocator* allocator
		= BufferAllocator::For(hardware->GbmDeviceFor(where));
	if (allocator == NULL)
		return 0;

	uint64 render[kBufferMaxModifiers];
	const int32 renderCount = allocator->RenderModifiers(kSwapFormatXRGB8888,
		render, kBufferMaxModifiers);
	if (renderCount <= 0)
		return 0;

	uint64 scanout[kBufferMaxModifiers];
	uint64 both[kBufferMaxModifiers];
	const uint64* from = render;
	int32 fromCount = renderCount;
	if (planeCapable) {
		const int32 scanoutCount = hardware->ScanoutModifiers(where,
			kSwapFormatXRGB8888, screenCapable, scanout,
			kBufferMaxModifiers);
		if (scanoutCount <= 0)
			return 0;
		fromCount = BufferAllocator::Intersect(render, renderCount, scanout,
			scanoutCount, both, kBufferMaxModifiers);
		from = both;
	}

	const int32 n = BufferAllocator::Intersect(from, fromCount, requested,
		requestedCount, result, kBufferMaxModifiers);
	// Only linear left is no negotiation.
	for (int32 i = 0; i < n; i++) {
		if (result[i] != kSwapModifierLinear)
			return n;
	}
	return 0;
#else
	(void)hardware; (void)where; (void)planeCapable; (void)screenCapable;
	(void)usage; (void)requested; (void)requestedCount; (void)result;
	return 0;
#endif
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

	// Only a chain a plane or a whole output can take needs scanout memory;
	// the CPU copy reads the rest, best kept in system memory.
	const bool planeCapable = hardware->HasOverlayPlane(where)
		|| hardware->ScanoutCapable(width, height);

	// Tiled buffers when the client only renders to them with the GPU.
	uint64 modifiers[BPrivate::kBufferMaxModifiers];
	const int32 modifierCount = negotiate_modifiers(hardware, where,
		hardware->HasOverlayPlane(where),
		hardware->ScanoutCapable(width, height), fUsage, fRequested,
		fRequestedCount, modifiers);

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
			where.IsValid() ? &where : NULL, planeCapable, modifiers,
			modifierCount);
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
	if (fImportPort < 0)
		fImportPort = create_port(8, "swap chain import");
	_ResetExternal();
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
		const GbmMemory* memory = bitmap->GbmBuffer();
		sender.Attach<uint64>(memory != NULL ? memory->Modifier()
			: kSwapModifierLinear);
		sender.Attach<uint32>((uint32)bitmap->BytesPerRow());
		sender.Attach<uint32>(memory != NULL ? memory->Offset() : 0);
	}
	sender.Attach<port_id>(fImportPort);
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
				|| generation->held[index]
				|| generation->buffers[index].Get() == NULL) {
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
		frame.token = generation->token;
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

	// An explicit fence rode in on the dma-buf: nothing is shown or copied
	// before the client's GPU work is done. Outside fLock, bounded.
	if ((flags & kPresentHasFence) != 0)
		wait_for_writer(frame.bitmap);

	// Same buffer geometry on the same plane and nothing changed under
	// the view: what the plane was given still holds.
	BRegion lazyClipping;
	PlaneConsumer* plane = fPlane;
	const bool steady = clipping == NULL && plane != NULL && plane->Active()
		&& plane->Token() == frame.token
		&& plane->Destination() == frame.destination;
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
			B_URGENT_DISPLAY_PRIORITY, this);
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

	if (fStats && sFreshSince.load() == 0)
		sFreshSince = system_time();
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
	sFreshSince = 0;
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

			fWakeAt = system_time();
			fDeadline = fWakeAt + fLead;
			fWindow->PostMessage(AS_INTERNAL_FLUSH_SWAP_CHAINS, 0);
			next = _NextFlush(system_time());
		}
		guard.lock();
	}
}


// Phase-locked to the last flip, early enough for the read and draw to land
// before the next compose; a timer when no flips arrive.
bigtime_t
ViewSwapChain::_NextFlush(bigtime_t now)
{
	const bigtime_t period = fPeriod > 0 ? fPeriod : 16667;
	::HWInterface* hardware = fWindow->Desktop()->HWInterface();
	const bigtime_t flip = hardware != NULL ? hardware->LastFlipTime() : 0;
	if (flip == 0 || now - flip > 8 * period)
		return now + period;

	const bigtime_t lead = fLead;
	bigtime_t next = flip - lead;
	if (next < now)
		next += ((now - next) / period + 1) * period;
	return next;
}


// Tracks the pump-to-published latency; widens at once on a miss, shrinks
// slowly. fLock held.
void
ViewSwapChain::_UpdateLead(bigtime_t done)
{
	const bigtime_t period = fPeriod > 0 ? fPeriod : 16667;
	const bigtime_t latency = done - fWakeAt;
	const bool missed = done > fDeadline;
	fWakeAt = 0;
	fLatency = fLatency == 0 ? latency : (fLatency * 7 + latency) / 8;

	bigtime_t want = (missed ? latency : fLatency) + 1000 + fLatency / 4;
	if (missed && want <= fLead)
		want = fLead + 1000;
	if (want >= fLead)
		fLead = want;
	else
		fLead -= (fLead - want) / 16;
	if (fLead < 2000)
		fLead = 2000;
	if (fLead > period / 2)
		fLead = period / 2;
}


// The window thread, drawing as for Present.
void
ViewSwapChain::Flush(DrawingEngine* engine)
{
	fFlushShown = false;
	_FlushFrame(engine);
	if (fFlushShown) {
		const bigtime_t done = system_time();
		BAutolock locker(fLock);
		if (fWakeAt != 0)
			_UpdateLead(done);
	}
}


void
ViewSwapChain::_FlushFrame(DrawingEngine* engine)
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
		fFlushShown = true;
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
		// A tiled import has no CPU pages: only the GPU reads it right.
		const bool wantGpu = bitmap->Bits() == NULL
			|| fPath == kTrialGpu || fPath == kPathGpu;
		if (wantGpu && hardware != NULL && fGpu == NULL)
			fGpu = new(std::nothrow) SwapChainGpuCopy();
		if (wantGpu && fGpu != NULL && !fGpu->Failed()
				&& fGpu->Copy(hardware->GbmDeviceFor(destination),
					bitmap.Get())) {
			gpu = fGpu;
			gpuTime = fGpu->Time();
		}
		if (wantGpu && gpu == NULL && fPath == kTrialGpu) {
			// Nothing to measure against.
			fPath = kPathCpu;
			fprintf(stderr, "swap chain %s: path cpu (gpu copy unavailable)\n",
				fView != NULL ? fView->Name() : "?");
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
		if (shadow.Get() != NULL && bitmap->Bits() != NULL)
			_Measure(gpu != NULL, frame.readTime);
	}
	frame.source = shadow.Get();
	fCpu->Show(frame);
	_CountCopy(frame.waitTime, frame.readTime, frame.copyTime, gpuTime);
}


void
ViewSwapChain::ReceiveExternal()
{
	if (fImportPort < 0)
		return;

	while (true) {
		struct {
			int32						slot;
			swap_chain_external			buffer;
		} message;
		int fds[PORT_MAX_FDS];
		int32 fdCount = PORT_MAX_FDS;
		int32 code;
		ssize_t size = read_port_fds(fImportPort, &code, &message,
			sizeof(message), fds, &fdCount, B_RELATIVE_TIMEOUT, 0);
		if (size < 0)
			return;

		if (size == (ssize_t)sizeof(message) && fdCount == 1)
			_ImportExternal(fds[0], message.slot, message.buffer);
		else if (size == (ssize_t)sizeof(message) && message.slot < 0) {
			BAutolock locker(fLock);
			_ResetExternal();
		}
		for (int32 i = 0; i < fdCount; i++)
			close(fds[i]);
	}
}


// Slots arrive in order; anything else the server cannot show is refused,
// and the client keeps copying.
void
ViewSwapChain::_ImportExternal(int fd, int32 slot,
	const swap_chain_external& buffer)
{
	::HWInterface* hardware = fWindow->Desktop()->HWInterface();
	BAutolock locker(fLock);
	if (fCurrent == NULL || hardware == NULL)
		return;

	const int32 have = fExternal != NULL ? fExternal->count : 0;
	const bool valid = slot == have && slot < kSwapChainMaxExternal
		&& buffer.fourcc == kSwapFormatXRGB8888
		&& (int32)buffer.width == fCurrent->width
		&& (int32)buffer.height == fCurrent->height
		&& buffer.stride >= buffer.width * 4;

	ServerBitmap* bitmap = NULL;
	if (valid) {
		bitmap = gBitmapManager->ImportDmabuf(*hardware, fd, buffer.width,
			buffer.height, buffer.fourcc, buffer.stride, buffer.offset,
			buffer.modifier, _DestinationFor(fCurrent->width,
				fCurrent->height));
	}

	if (bitmap == NULL) {
		if (!fExternalWarned) {
			fExternalWarned = true;
			fprintf(stderr, "swap chain: imported buffer refused (slot %d, "
				"%ux%u, modifier %#llx)\n", (int)slot, (unsigned)buffer.width,
				(unsigned)buffer.height, (unsigned long long)buffer.modifier);
		}
		__atomic_store_n(&fCurrent->state->externalRefused, (int32)1,
			__ATOMIC_RELEASE);
		return;
	}

	if (fExternal == NULL) {
		fExternal = new(std::nothrow) Generation;
		if (fExternal == NULL) {
			BReference<ServerBitmap> release(bitmap, true);
			return;
		}
		fExternal->token = atomic_add(&sNextToken, 1) + 1;
		fExternal->width = fCurrent->width;
		fExternal->height = fCurrent->height;
		fExternal->external = true;
	}
	fExternal->buffers[slot].SetTo(bitmap, true);
	fExternal->count = slot + 1;
}


// The set goes to the generation list, which frees it once nothing shows
// or holds a buffer of it. fLock held.
void
ViewSwapChain::_ResetExternal()
{
	if (fExternal == NULL)
		return;

	Generation* old = fExternal;
	fExternal = NULL;
	try {
		fGenerations.push_back(old);
	} catch (std::bad_alloc&) {
		// Still referenced when shown or pending; leak nothing otherwise.
		if (old != fShownGeneration && fPending.empty())
			delete old;
	}
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
#ifdef HAVE_GBM
	// A tiled buffer is read through the detiling map.
	GbmMemory* tiled = bitmap->GbmBuffer();
	if (tiled != NULL && !tiled->IsLinear() && tiled->BufferObject() != NULL) {
		uint32 mapStride = 0;
		void* mapData = NULL;
		void* map = BufferAllocator::Map(tiled->BufferObject(), 0, 0,
			bitmap->Width(), bitmap->Height(), false, &mapStride, &mapData);
		if (map == NULL)
			return;
		for (int32 i = 0; i < visible.CountRects(); i++) {
			const BRect rect = visible.RectAt(i);
			target->ImportBits(map, (size_t)mapStride * bitmap->Height(),
				mapStride, bitmap->ColorSpace(),
				BPoint(rect.left - destination.left,
					rect.top - destination.top),
				BPoint(rect.left - bounds.left, rect.top - bounds.top),
				rect.IntegerWidth() + 1, rect.IntegerHeight() + 1);
		}
		BufferAllocator::Unmap(tiled->BufferObject(), mapData);
		return;
	}
#endif
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
	if (token < 0) {
		return fExternal != NULL && fCurrent != NULL
			&& -token == fCurrent->token ? fExternal : NULL;
	}

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
	if (generation->external) {
		// A replaced set has nobody left to tell.
		if (generation == fExternal && fCurrent != NULL) {
			swap_chain_state* state = fCurrent->state;
			state->releaseTime = when;
			state->refreshPeriod = period;
			__atomic_store_n(&state->external[index], (int32)kSwapBufferFree,
				__ATOMIC_RELEASE);
		}
		return;
	}

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
	// Idle stretches are not gaps between shown frames.
	if (fLastShown != 0 && when - fLastShown < 250000
			&& when - fLastShown > fWorstGap)
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

	if (bitmap == NULL || bitmap->ColorSpace() != B_RGB32
			|| (bitmap->Bits() == NULL && bitmap->GbmBuffer() == NULL)) {
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
	fprintf(stderr, "swap chain %s: path %s, shown %llu/s, presented %llu/s, "
		"replaced %u, worst gap %.1f ms, wait %lld us, read %lld us, copy %lld us, gpu %lld us, lead %lld us\n",
		fView != NULL ? fView->Name() : "?", _PathName(),
		(unsigned long long)fShown * 1000000 / span,
		(unsigned long long)fPresented * 1000000 / span, (unsigned)fReplaced,
		fWorstGap / 1000.0,
		(long long)(copies > 0 ? waitTime / copies : 0),
		(long long)(copies > 0 ? readTime / copies : 0),
		(long long)(copies > 0 ? copyTime / copies : 0),
		(long long)(copies > 0 ? gpuTime / copies : 0), (long long)fLead);
	fStatsStart = now;
	fShown = fPresented = fReplaced = 0;
	fWorstGap = 0;
}


// The pump thread. The first frames of each read are warm-up.
void
ViewSwapChain::_Measure(bool gpu, bigtime_t time)
{
	const int32 kWarmup = 3;
	const int32 kFrames = 30;
	if (fPath != kTrialGpu && fPath != kTrialCpu)
		return;
	if ((fPath == kTrialGpu) != gpu)
		return;

	fTrialFrames++;
	if (fTrialFrames > kWarmup)
		fTrialTime[gpu ? 0 : 1] += time;
	if (fTrialFrames < kWarmup + kFrames)
		return;

	fTrialFrames = 0;
	if (fPath == kTrialGpu) {
		fPath = kTrialCpu;
		return;
	}

	const int64 gpuTime = fTrialTime[0] / kFrames;
	const int64 cpuTime = fTrialTime[1] / kFrames;
	fPath = gpuTime < cpuTime ? kPathGpu : kPathCpu;
	fprintf(stderr, "swap chain %s: path %s (gpu %lld us, cpu %lld us, "
		"%d frames each)\n", fView != NULL ? fView->Name() : "?",
		fPath == kPathGpu ? "gpu" : "cpu", (long long)gpuTime,
		(long long)cpuTime, (int)kFrames);
}


const char*
ViewSwapChain::_PathName() const
{
	PlaneConsumer* plane = fPlane;
	if (plane != NULL && plane->Active())
		return plane->Scanout() ? "scanout" : "plane";
	return fPath == kPathGpu || fPath == kTrialGpu ? "gpu" : "cpu";
}


bigtime_t
ViewSwapChain::FreshSince()
{
	return sFreshSince.load();
}


void
ViewSwapChain::Snapshot(std::vector<DisplayChainProfile>& chains)
{
	std::lock_guard<std::mutex> guard(sChainsMutex);
	for (size_t i = 0; i < sChains.size(); i++) {
		DisplayChainProfile chain;
		sChains[i]->_Snapshot(chain);
		chains.push_back(chain);
	}
}


void
ViewSwapChain::_Snapshot(DisplayChainProfile& chain) const
{
	const bool locked = fLock.LockWithTimeout(20000) == B_OK;
	chain.view = fView != NULL ? fView->Name() : "?";
	chain.path = _PathName();
	chain.trial = fPath == kTrialGpu || fPath == kTrialCpu;
	chain.left = (int32)fPeriodFrame.left;
	chain.top = (int32)fPeriodFrame.top;
	chain.right = (int32)fPeriodFrame.right;
	chain.bottom = (int32)fPeriodFrame.bottom;
	chain.period = fPeriod;
	chain.lead = fLead;
	chain.latency = fLatency;
	chain.gpuCost = fTrialTime[0] / 30;
	chain.cpuCost = fTrialTime[1] / 30;
	chain.statsOn = fStats;
	chain.shown = fShown;
	chain.presented = fPresented;
	chain.replaced = fReplaced;
	chain.worstGap = fWorstGap;
	if (locked)
		fLock.Unlock();
}


void
ViewSwapChain::_UpdatePeriod(::HWInterface* hardware)
{
	if (hardware == NULL)
		return;

	// The mode changes rarely; the window may move between outputs.
	const bigtime_t now = system_time();
	const BRect frame = fWindow->Window() != NULL
		? fWindow->Window()->Frame() : BRect();
	if (fPeriod != 0 && frame == fPeriodFrame && now - fPeriodAt < 1000000)
		return;
	fPeriodAt = now;
	fPeriodFrame = frame;

	fPeriod = (int32)hardware->RefreshPeriod(frame);
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
