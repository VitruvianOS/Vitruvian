/*
 * Copyright 2026, VitruvianOS Project.
 * Distributed under the terms of the MIT License.
 *
 * wpe-backend-vos — Wayland-free WPE backend for Vitruvian app_server.
 *
 * This library replaces wpebackend-fdo for the SHM frame-export path.
 * Instead of routing through an in-process wl_display / wl_shm_buffer,
 * rendered frames arrive as plain pixel buffers that the embedder
 * copies directly into a BBitmap (or equivalent).
 *
 * The backend creates a wpe_view_backend via libwpe's
 * wpe_view_backend_create_with_backend_interface(), providing a minimal
 * software-renderer path.  When WebKit paints a frame the backend
 * captures the resulting pixel data into an internal buffer and hands it
 * to the embedder through the export_buffer callback — no Wayland
 * protocol objects are involved at any point.
 */

#include "wpe-backend-vos.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

#include <wpe/wpe.h>

/* ======================================================================
 * Internal types
 * ====================================================================== */

/* Maximum number of buffers we hand out simultaneously.  The embedder
 * may hold one while we fill the next. */
enum { POOL_SIZE = 3 };

struct ExportedBuffer {
	const uint8_t*	data;
	uint32_t	width;
	uint32_t	height;
	uint32_t	stride;		/* bytes per row, always width*4 (BGRA) */
	bool		inUse;
};

struct wpe_backend_vos_exportable {
	const wpe_backend_vos_exportable_client*	client;
	void*					userData;

	ExportedBuffer				pool[POOL_SIZE];
	int					fillIdx;

	wpe_view_backend*			backend;
	uint32_t				width;
	uint32_t				height;
};

/* ======================================================================
 * Buffer management helpers
 * ====================================================================== */

static ExportedBuffer*
acquire_buffer(wpe_backend_vos_exportable* e)
{
	for (int i = 0; i < POOL_SIZE; i++) {
		if (!e->pool[i].inUse) {
			e->pool[i].inUse = true;
			return &e->pool[i];
		}
	}
	/* All buffers busy — steal the oldest fill. */
	ExportedBuffer* b = &e->pool[e->fillIdx];
	e->fillIdx = (e->fillIdx + 1) % POOL_SIZE;
	b->inUse = true;
	return b;
}

/* ======================================================================
 * wpe_view_backend_client callbacks
 * ====================================================================== */

static void
cb_set_size(void* data, uint32_t w, uint32_t h)
{
	auto* e = static_cast<wpe_backend_vos_exportable*>(data);
	e->width  = w;
	e->height = h;
}

static void
cb_frame_displayed(void* /*data*/) { }

static void
cb_activity_state_changed(void* /*data*/, uint32_t /*state*/) { }

static void*
cb_get_accessible(void* /*data*/) { return nullptr; }

static void
cb_set_device_scale_factor(void* /*data*/, float /*f*/) { }

static void
cb_target_refresh_rate_changed(void* /*data*/, uint32_t /*r*/) { }

static const wpe_view_backend_client s_backend_client = {
	cb_set_size,
	cb_frame_displayed,
	cb_activity_state_changed,
	cb_get_accessible,
	cb_set_device_scale_factor,
	cb_target_refresh_rate_changed,
};

/* ======================================================================
 * wpe_view_backend_interface callbacks
 * ====================================================================== */

static void*
iface_create(void* data, wpe_view_backend* backend)
{
	auto* e = static_cast<wpe_backend_vos_exportable*>(data);
	e->backend = backend;
	return e;
}

static void
iface_destroy(void* /*data*/) { }

static void
iface_initialize(void* /*data*/) { }

static int
iface_get_renderer_host_fd(void* /*data*/)
{
	/* Software path — no GPU fd. */
	return -1;
}

static const wpe_view_backend_interface s_backend_interface = {
	iface_create,
	iface_destroy,
	iface_initialize,
	iface_get_renderer_host_fd,
};

/* ======================================================================
 * Public API — exportable lifecycle
 * ====================================================================== */

extern "C" WPE_EXPORT
wpe_backend_vos_exportable*
wpe_backend_vos_exportable_create(
	const wpe_backend_vos_exportable_client* client,
	void* userData, uint32_t width, uint32_t height)
{
	auto* e = new wpe_backend_vos_exportable;
	std::memset(e, 0, sizeof(*e));

	e->client    = client;
	e->userData  = userData;
	e->width     = width;
	e->height    = height;
	e->fillIdx   = 0;
	e->backend   = nullptr;

	for (int i = 0; i < POOL_SIZE; i++) {
		e->pool[i].data   = nullptr;
		e->pool[i].width  = 0;
		e->pool[i].height = 0;
		e->pool[i].stride = 0;
		e->pool[i].inUse  = false;
	}

	return e;
}

extern "C" WPE_EXPORT
void
wpe_backend_vos_exportable_destroy(wpe_backend_vos_exportable* e)
{
	if (!e) return;

	/* Free any pooled pixel data. */
	for (int i = 0; i < POOL_SIZE; i++)
		delete[] e->pool[i].data;

	/* Destroy the view backend if we own it. */
	if (e->backend)
		wpe_view_backend_destroy(e->backend);

	delete e;
}

extern "C" WPE_EXPORT
wpe_view_backend*
wpe_backend_vos_exportable_get_view_backend(wpe_backend_vos_exportable* e)
{
	if (!e) return nullptr;

	wpe_view_backend* b =
		wpe_view_backend_create_with_backend_interface(
			&s_backend_interface, e);

	if (b)
		wpe_view_backend_set_backend_client(b, &s_backend_client, e);

	return b;
}

/* ======================================================================
 * Public API — frame flow
 * ====================================================================== */

extern "C" WPE_EXPORT
void
wpe_backend_vos_exportable_dispatch_frame_complete(
	wpe_backend_vos_exportable* /*e*/)
{
	/* Called by the embedder after it is done with the current frame. */
}

extern "C" WPE_EXPORT
void
wpe_backend_vos_exportable_release_buffer(
	wpe_backend_vos_exportable* /*e*/,
	wpe_backend_vos_exported_buffer* buf)
{
	if (!buf) return;
	auto* eb = reinterpret_cast<ExportedBuffer*>(buf);
	eb->inUse = false;
}

/* ======================================================================
 * Public API — exported buffer accessors
 * ====================================================================== */

extern "C" WPE_EXPORT
const uint8_t*
wpe_backend_vos_exported_buffer_get_data(
	wpe_backend_vos_exported_buffer* buf)
{
	return buf ? reinterpret_cast<ExportedBuffer*>(buf)->data : nullptr;
}

extern "C" WPE_EXPORT
uint32_t
wpe_backend_vos_exported_buffer_get_width(
	wpe_backend_vos_exported_buffer* buf)
{
	return buf ? reinterpret_cast<ExportedBuffer*>(buf)->width : 0;
}

extern "C" WPE_EXPORT
uint32_t
wpe_backend_vos_exported_buffer_get_height(
	wpe_backend_vos_exported_buffer* buf)
{
	return buf ? reinterpret_cast<ExportedBuffer*>(buf)->height : 0;
}

extern "C" WPE_EXPORT
uint32_t
wpe_backend_vos_exported_buffer_get_stride(
	wpe_backend_vos_exported_buffer* buf)
{
	return buf ? reinterpret_cast<ExportedBuffer*>(buf)->stride : 0;
}

/* ======================================================================
 * Helper — submit a rendered frame from the backend to the embedder.
 *
 * This is called internally by the backend when WebKit finishes
 * compositing a frame.  The pixel data is copied into a pooled buffer
 * and handed to the export_buffer callback.
 * ====================================================================== */

void
wpe_backend_vos_exportable_submit_frame(
	wpe_backend_vos_exportable* e,
	const uint8_t* pixels, uint32_t w, uint32_t h, uint32_t stride)
{
	if (!e || !e->client || !e->client->export_buffer)
		return;

	ExportedBuffer* buf = acquire_buffer(e);

	/* Ensure the buffer is large enough. */
	uint32_t bytes = stride * h;
	if (buf->data && buf->width == w && buf->height == h
		&& buf->stride == stride) {
		/* Reuse existing allocation. */
		std::memcpy(const_cast<uint8_t*>(buf->data), pixels, bytes);
	} else {
		delete[] buf->data;
		auto* copy = new uint8_t[bytes];
		std::memcpy(copy, pixels, bytes);
		buf->data   = copy;
		buf->width  = w;
		buf->height = h;
		buf->stride = stride;
	}

	e->client->export_buffer(e->userData,
		reinterpret_cast<wpe_backend_vos_exported_buffer*>(buf));
}
