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
 * ABI note: all symbols use the wpe_backend_vos_ prefix so they never
 * clash with wpebackend-fdo.  The view_backend returned by
 * wpe_backend_vos_exportable_get_view_backend() is a standard
 * wpe_view_backend* that can be handed straight to
 * webkit_web_view_backend_new().
 */
#ifndef WPE_BACKEND_VOS_H
#define WPE_BACKEND_VOS_H

#include <stdint.h>

/* Provide WPE_EXPORT if not already defined by wpe/wpe.h. */
#ifndef WPE_EXPORT
#  if defined(__GNUC__) || defined(__clang__)
#    define WPE_EXPORT __attribute__((visibility("default")))
#  else
#    define WPE_EXPORT
#  endif
#endif

/* Forward-declare the libwpe view_backend without pulling in wpe/wpe.h. */
struct wpe_view_backend;

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Opaque types                                                       */
/* ------------------------------------------------------------------ */

struct wpe_backend_vos_exportable;
struct wpe_backend_vos_exported_buffer;

/* ------------------------------------------------------------------ */
/*  Export client — the embedder provides this callbacks struct.        */
/* ------------------------------------------------------------------ */

struct wpe_backend_vos_exportable_client {
	/*
	 * export_buffer: called on the GLib thread each time WebKit
	 * finishes a frame.  @buffer is valid until the embedder calls
	 * wpe_backend_vos_exportable_release_buffer().
	 */
	void (*export_buffer)(void *data,
			     struct wpe_backend_vos_exported_buffer *buffer);

	/* reserved for future use (dmabuf, etc.) */
	void (*_wpe_reserved0)(void);
	void (*_wpe_reserved1)(void);
};

/* ------------------------------------------------------------------ */
/*  Exportable lifecycle                                                */
/* ------------------------------------------------------------------ */

WPE_EXPORT
struct wpe_backend_vos_exportable *
wpe_backend_vos_exportable_create(
	const struct wpe_backend_vos_exportable_client *client,
	void *user_data, uint32_t width, uint32_t height);

WPE_EXPORT
void
wpe_backend_vos_exportable_destroy(
	struct wpe_backend_vos_exportable *exportable);

WPE_EXPORT
struct wpe_view_backend *
wpe_backend_vos_exportable_get_view_backend(
	struct wpe_backend_vos_exportable *exportable);

/* ------------------------------------------------------------------ */
/*  Frame flow                                                          */
/* ------------------------------------------------------------------ */

/*
 * The backend calls this internally when a frame is complete — the
 * embedder normally does NOT need to call it directly.
 *
 * However, if the embedder wants to drive frame timing manually
 * (e.g. for testing), it may call this after the export_buffer
 * callback returns.
 */
WPE_EXPORT
void
wpe_backend_vos_exportable_dispatch_frame_complete(
	struct wpe_backend_vos_exportable *exportable);

/*
 * Release a buffer previously received via the export_buffer callback.
 * Must be called exactly once per buffer.
 */
WPE_EXPORT
void
wpe_backend_vos_exportable_release_buffer(
	struct wpe_backend_vos_exportable *exportable,
	struct wpe_backend_vos_exported_buffer *buffer);

/* ------------------------------------------------------------------ */
/*  Exported buffer accessors                                           */
/* ------------------------------------------------------------------ */

WPE_EXPORT
const uint8_t *
wpe_backend_vos_exported_buffer_get_data(
	struct wpe_backend_vos_exported_buffer *buffer);

WPE_EXPORT
uint32_t
wpe_backend_vos_exported_buffer_get_width(
	struct wpe_backend_vos_exported_buffer *buffer);

WPE_EXPORT
uint32_t
wpe_backend_vos_exported_buffer_get_height(
	struct wpe_backend_vos_exported_buffer *buffer);

WPE_EXPORT
uint32_t
wpe_backend_vos_exported_buffer_get_stride(
	struct wpe_backend_vos_exported_buffer *buffer);

/* ------------------------------------------------------------------ */
/*  Frame submission — called by the backend internally                 */
/* ------------------------------------------------------------------ */

/*
 * Submit a rendered frame to the embedder.  The pixel data (BGRA,
 * top-to-bottom) is copied into an internal buffer and passed to the
 * export_buffer callback.  This is NOT part of the public ABI — it is
 * called by backend implementation code.
 */
WPE_EXPORT
void
wpe_backend_vos_exportable_submit_frame(
	struct wpe_backend_vos_exportable *exportable,
	const uint8_t *pixels, uint32_t width, uint32_t height,
	uint32_t stride);

#ifdef __cplusplus
}
#endif

#endif /* WPE_BACKEND_VOS_H */
