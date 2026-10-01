/*
 * Copyright 2026, VitruvianOS Project.
 * Distributed under the terms of the LGPL License.
 */


#include "compositor_adapter.h"
#include "logging.h"

#include <Bitmap.h>
#include <GraphicsDefs.h>
#include <Rect.h>

// wpe-backend-vos provides a Wayland-free frame-export path.
// Rendered frames arrive as plain pixel buffers that we copy directly
// into BBitmap for Vitruvian's app_server.

#include <algorithm>
#include <cstdlib>
#include <cstring>


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
	struct wpe_backend_vos_exportable_client exportClient;
	memset(&exportClient, 0, sizeof(exportClient));

	exportClient.export_buffer =
		[](void* data, struct wpe_backend_vos_exported_buffer* buffer) {
			auto* self = static_cast<CompositorAdapter*>(data);
			const uint8_t* pixels =
				wpe_backend_vos_exported_buffer_get_data(buffer);
			int32_t srcW = (int32_t)
				wpe_backend_vos_exported_buffer_get_width(buffer);
			int32_t srcH = (int32_t)
				wpe_backend_vos_exported_buffer_get_height(buffer);
			int32_t stride = (int32_t)
				wpe_backend_vos_exported_buffer_get_stride(buffer);
			if (pixels && srcW > 0 && srcH > 0)
				self->_BlitSHMFrame(pixels, stride, srcW, srcH);
			wpe_backend_vos_exportable_release_buffer(
				self->fExportable, buffer);
			wpe_backend_vos_exportable_dispatch_frame_complete(
				self->fExportable);
		};

	fExportable = wpe_backend_vos_exportable_create(
		&exportClient, this, (uint32_t)w, (uint32_t)h);
	if (!fExportable) {
		log_error("CompositorAdapter: failed to create VOS exportable");
		return;
	}

	// VOS backend creates and owns the backend; do not call
	// wpe_view_backend_initialize.
	fBackend = wpe_backend_vos_exportable_get_view_backend(fExportable);

	_AllocateBitmaps();

	log_info("CompositorAdapter: created %dx%d (VOS/SHM)", w, h);
}


CompositorAdapter::~CompositorAdapter()
{
	if (fExportable)
		wpe_backend_vos_exportable_destroy(fExportable);
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
