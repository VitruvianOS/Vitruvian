/*
 * Copyright 2026, VitruvianOS Project.
 * Distributed under the terms of the LGPL License.
 */


#include "compositor_adapter.h"
#include "logging.h"

#include <Bitmap.h>
#include <GraphicsDefs.h>
#include <Rect.h>

// WPEBackend-FDO creates its own internal wl_display in-process — no external
// Wayland compositor is required. The wl_shm_buffer below is a pure in-process
// object; we use it only to access the raw pixel pointer and then copy into a
// BBitmap for Vitruvian's app_server. No Wayland protocol traffic leaves this
// process.
#include <wayland-server.h>
#include <wpe/fdo.h>
#include <wpe/unstable/fdo-shm.h>
#include <wpe/wpe.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>


// The web process loads its renderer backend through libwpe; without an
// explicit loader init libwpe looks for libWPEBackend-default.so and aborts.
static void
init_fdo_backend()
{
	static bool sDone = false;
	if (sDone)
		return;
	sDone = true;
	wpe_loader_init("libWPEBackend-fdo-1.0.so.1");
	wpe_fdo_initialize_shm();
}


CompositorAdapter::CompositorAdapter(int w, int h)
	:
	fExportable(nullptr),
	fBackend(nullptr),
	frontBitmap(nullptr),
	fBackBitmap(nullptr),
	fWidth(w),
	fHeight(h),
	fFrameReady(false)
{
	init_fdo_backend();

	// fdo keeps this pointer for the exportable's lifetime: it must not
	// live on the stack.
	static struct wpe_view_backend_exportable_fdo_client exportClient;
	memset(&exportClient, 0, sizeof(exportClient));

	exportClient.export_shm_buffer =
		[](void* data, struct wpe_fdo_shm_exported_buffer* buffer) {
			auto* self = static_cast<CompositorAdapter*>(data);
			struct wl_shm_buffer* shmBuf =
				wpe_fdo_shm_exported_buffer_get_shm_buffer(buffer);
			if (shmBuf) {
				wl_shm_buffer_begin_access(shmBuf);
				const uint8_t* pixels =
					static_cast<const uint8_t*>(wl_shm_buffer_get_data(shmBuf));
				int32_t srcW = (int32_t)wl_shm_buffer_get_width(shmBuf);
				int32_t srcH = (int32_t)wl_shm_buffer_get_height(shmBuf);
				int32_t stride = wl_shm_buffer_get_stride(shmBuf);
				self->_BlitSHMFrame(pixels, stride, srcW, srcH);
				wl_shm_buffer_end_access(shmBuf);
			}
			wpe_view_backend_exportable_fdo_dispatch_release_shm_exported_buffer(
				self->fExportable, buffer);
			wpe_view_backend_exportable_fdo_dispatch_frame_complete(
				self->fExportable);
		};

	fExportable = wpe_view_backend_exportable_fdo_create(
		&exportClient, this, (uint32_t)w, (uint32_t)h);
	if (!fExportable) {
		log_error("CompositorAdapter: failed to create FDO exportable");
		return;
	}

	// FDO creates and owns the backend; do not call wpe_view_backend_initialize.
	fBackend = wpe_view_backend_exportable_fdo_get_view_backend(fExportable);

	_AllocateBitmaps();

	log_info("CompositorAdapter: created %dx%d (FDO/SHM)", w, h);
}


CompositorAdapter::~CompositorAdapter()
{
	if (fExportable)
		wpe_view_backend_exportable_fdo_destroy(fExportable);
	delete frontBitmap;
	delete fBackBitmap;
}


void
CompositorAdapter::_AllocateBitmaps()
{
	BRect bounds(0, 0, fWidth - 1, fHeight - 1);

	delete frontBitmap;
	delete fBackBitmap;

	frontBitmap = new BBitmap(bounds, 0, B_RGB32);
	fBackBitmap = new BBitmap(bounds, 0, B_RGB32);

	if (frontBitmap->InitCheck() != B_OK)
		log_error("CompositorAdapter: front bitmap allocation failed");
	if (fBackBitmap->InitCheck() != B_OK)
		log_error("CompositorAdapter: back bitmap allocation failed");

	if (frontBitmap->Bits())
		memset(frontBitmap->Bits(), 0xFF, frontBitmap->BitsLength());
	if (fBackBitmap->Bits())
		memset(fBackBitmap->Bits(), 0xFF, fBackBitmap->BitsLength());
}


void
CompositorAdapter::Resize(int newWidth, int newHeight)
{
	if (newWidth == fWidth && newHeight == fHeight)
		return;

	log_info("CompositorAdapter::Resize %dx%d -> %dx%d",
		fWidth, fHeight, newWidth, newHeight);

	std::lock_guard<std::mutex> lock(fBitmapMutex);
	fWidth = newWidth;
	fHeight = newHeight;
	_AllocateBitmaps();
	// Caller (WebKitBridge::Resize) dispatches set_size to WPE on the GLib thread.
}


void
CompositorAdapter::_SwapBuffersLocked()
{
	std::swap(frontBitmap, fBackBitmap);
	fFrameReady.store(false);
}


void
CompositorAdapter::_BlitSHMFrame(const uint8_t* data, int stride,
	int srcW, int srcH)
{
	std::function<void()> cb;
	{
		std::lock_guard<std::mutex> lock(fBitmapMutex);

		if (!fBackBitmap || !fBackBitmap->Bits())
			return;

		int copyW = std::min(srcW, fWidth) * 4;
		int copyH = std::min(srcH, fHeight);
		uint8_t* dst = (uint8_t*)fBackBitmap->Bits();
		int dstStride = fBackBitmap->BytesPerRow();

		for (int y = 0; y < copyH; y++)
			memcpy(dst + y * dstStride, data + y * stride, copyW);

		_SwapBuffersLocked();
		fFrameReady.store(true);
		cb = onFrameReady;
	}
	if (cb) cb();
}


BBitmap*
CompositorAdapter::AcquireFront()
{
	// BView::Draw runs on the window looper thread only; single acquirer.
	fBitmapMutex.lock();
	return frontBitmap;
}


void
CompositorAdapter::ReleaseFront()
{
	fBitmapMutex.unlock();
}
