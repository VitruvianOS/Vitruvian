/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "vos_platform.h"

#include "compositor_adapter.h"
#include "logging.h"

#include <cstring>


// 30 Hz matches the FDO frame clock: it leaves llvmpipe CPU for video
// decoding. The tick completes WebKit's buffer pipeline (FrameDone).
static const guint kVosFrameTickMs = 33;


struct _VosDisplay {
	WPEDisplay			parent;
	VosPlatformHost*	host;
};


struct _VosToplevel {
	WPEToplevel			parent;
	VosPlatformHost*	host;
};


struct _VosView {
	WPEView				parent;
	VosPlatformHost*	host;
	WPEBuffer*			pendingBuffer;
	guint				paceSource;
};


G_DEFINE_TYPE(VosDisplay, vos_display, WPE_TYPE_DISPLAY)
G_DEFINE_TYPE(VosToplevel, vos_toplevel, WPE_TYPE_TOPLEVEL)
G_DEFINE_TYPE(VosView, vos_view, WPE_TYPE_VIEW)


//	#pragma mark - VosView


static gboolean
_vos_view_pace_tick(gpointer data)
{
	auto* view = VOS_VIEW(data);
	view->paceSource = 0;
	if (view->pendingBuffer != nullptr) {
		// On screen: WebKit may produce the next frame, and this
		// buffer is free — the pixels were copied out already.
		wpe_view_buffer_rendered(WPE_VIEW(view), view->pendingBuffer);
		wpe_view_buffer_released(WPE_VIEW(view), view->pendingBuffer);
		g_object_unref(view->pendingBuffer);
		view->pendingBuffer = nullptr;
	}
	return G_SOURCE_REMOVE;
}


static gboolean
vos_view_render_buffer(WPEView* wpeView, WPEBuffer* buffer,
	const WPERectangle*, guint, GError** error)
{
	auto* view = VOS_VIEW(wpeView);
	if (!WPE_IS_BUFFER_SHM(buffer)) {
		g_set_error_literal(error, WPE_VIEW_ERROR,
			WPE_VIEW_ERROR_RENDER_FAILED,
			"VosView: only SHM buffers are supported");
		return FALSE;
	}

	WPEBufferSHM* shm = WPE_BUFFER_SHM(buffer);
	GBytes* bytes = wpe_buffer_shm_get_data(shm);
	if (bytes == nullptr)
		return FALSE;

	gsize size = 0;
	const uint8_t* data = static_cast<const uint8_t*>(g_bytes_get_data(bytes,
		&size));
	int width = wpe_buffer_get_width(buffer);
	int height = wpe_buffer_get_height(buffer);
	int stride = static_cast<int>(wpe_buffer_shm_get_stride(shm));

	if (view->host != nullptr && view->host->compositor != nullptr)
		view->host->compositor->PresentSHMFrame(data, stride, width, height);

	// Hold the buffer until the next tick: the renderer waits for
	// FrameDone, so the tick paces the whole pipeline.
	if (view->pendingBuffer != nullptr)
		g_object_unref(view->pendingBuffer);
	view->pendingBuffer = static_cast<WPEBuffer*>(g_object_ref(buffer));
	if (view->paceSource == 0) {
		view->paceSource = g_timeout_add(kVosFrameTickMs,
			_vos_view_pace_tick, view);
	}
	return TRUE;
}


static gboolean
vos_view_can_be_mapped(WPEView*)
{
	return TRUE;
}


static void
vos_view_set_cursor_from_name(WPEView* wpeView, const char* name)
{
	auto* view = VOS_VIEW(wpeView);
	if (view->host != nullptr && view->host->onCursorName)
		view->host->onCursorName(name != nullptr ? name : "");
}


static void
vos_view_dispose(GObject* object)
{
	auto* view = VOS_VIEW(object);
	if (view->paceSource != 0) {
		g_source_remove(view->paceSource);
		view->paceSource = 0;
	}
	if (view->pendingBuffer != nullptr) {
		g_object_unref(view->pendingBuffer);
		view->pendingBuffer = nullptr;
	}

	G_OBJECT_CLASS(vos_view_parent_class)->dispose(object);
}


static void
vos_view_constructed(GObject* object)
{
	G_OBJECT_CLASS(vos_view_parent_class)->constructed(object);

	// A fresh view is 0x0 and unmapped; attach to the toplevel the
	// display created for it and take its size, like headless does.
	auto* wpeView = WPE_VIEW(object);
	g_signal_connect(wpeView, "notify::toplevel", G_CALLBACK(+[](WPEView* wpeView,
		GParamSpec*, gpointer) {
		auto* toplevel = wpe_view_get_toplevel(wpeView);
		if (toplevel == nullptr) {
			wpe_view_unmap(wpeView);
			return;
		}

		int width = 0;
		int height = 0;
		wpe_toplevel_get_size(toplevel, &width, &height);
		if (width > 0 && height > 0)
			wpe_view_resized(wpeView, width, height);
		wpe_view_map(wpeView);
	}), nullptr);
}


static void
vos_view_class_init(VosViewClass* viewClass)
{
	GObjectClass* objectClass = G_OBJECT_CLASS(viewClass);
	objectClass->constructed = vos_view_constructed;
	objectClass->dispose = vos_view_dispose;

	WPEViewClass* wpeViewClass = WPE_VIEW_CLASS(viewClass);
	wpeViewClass->render_buffer = vos_view_render_buffer;
	wpeViewClass->can_be_mapped = vos_view_can_be_mapped;
	wpeViewClass->set_cursor_from_name = vos_view_set_cursor_from_name;
}


static void
vos_view_init(VosView* view)
{
	view->host = nullptr;
	view->pendingBuffer = nullptr;
	view->paceSource = 0;
}


//	#pragma mark - VosToplevel


static gboolean
vos_toplevel_resize(WPEToplevel* toplevel, int width, int height)
{
	auto* self = VOS_TOPLEVEL(toplevel);
	if (self->host != nullptr && self->host->onResizeRequest)
		self->host->onResizeRequest(width, height);
	return TRUE;
}


static gboolean
vos_toplevel_set_fullscreen(WPEToplevel* toplevel, gboolean fullscreen)
{
	auto* self = VOS_TOPLEVEL(toplevel);
	if (self->host == nullptr || !self->host->onFullscreenRequest)
		return FALSE;
	self->host->onFullscreenRequest(fullscreen != FALSE);
	return TRUE;
}


static void
vos_toplevel_constructed(GObject* object)
{
	G_OBJECT_CLASS(vos_toplevel_parent_class)->constructed(object);

	// The host window is always live; report the toplevel active so
	// pages see the window as focused once the view takes focus.
	wpe_toplevel_state_changed(WPE_TOPLEVEL(object), WPE_TOPLEVEL_STATE_ACTIVE);
}


static void
vos_toplevel_class_init(VosToplevelClass* toplevelClass)
{
	GObjectClass* objectClass = G_OBJECT_CLASS(toplevelClass);
	objectClass->constructed = vos_toplevel_constructed;

	WPEToplevelClass* wpeToplevelClass = WPE_TOPLEVEL_CLASS(toplevelClass);
	wpeToplevelClass->resize = vos_toplevel_resize;
	wpeToplevelClass->set_fullscreen = vos_toplevel_set_fullscreen;
}


static void
vos_toplevel_init(VosToplevel* toplevel)
{
	toplevel->host = nullptr;
}


//	#pragma mark - VosDisplay


static gboolean
vos_display_connect(WPEDisplay*, GError**)
{
	// The app_server surface is always there; there is nothing to
	// open and no socket to dial.
	return TRUE;
}


static WPEView*
vos_display_create_view(WPEDisplay* display)
{
	auto* self = VOS_DISPLAY(display);
	auto* view = VOS_VIEW(g_object_new(VOS_TYPE_VIEW, "display", display,
		nullptr));
	view->host = self->host;
	return WPE_VIEW(view);
}


static WPEToplevel*
vos_display_create_toplevel(WPEDisplay* display, guint)
{
	auto* self = VOS_DISPLAY(display);
	auto* toplevel = VOS_TOPLEVEL(g_object_new(VOS_TYPE_TOPLEVEL, "display",
		display, nullptr));
	toplevel->host = self->host;
	return WPE_TOPLEVEL(toplevel);
}


static void
vos_display_class_init(VosDisplayClass* displayClass)
{
	WPEDisplayClass* wpeDisplayClass = WPE_DISPLAY_CLASS(displayClass);
	wpeDisplayClass->connect = vos_display_connect;
	wpeDisplayClass->create_view = vos_display_create_view;
	wpeDisplayClass->create_toplevel = vos_display_create_toplevel;
}


static void
vos_display_init(VosDisplay* display)
{
	display->host = nullptr;
}


WPEDisplay*
vos_display_new(VosPlatformHost* host)
{
	auto* display = VOS_DISPLAY(g_object_new(VOS_TYPE_DISPLAY, nullptr));
	display->host = host;
	log_info("VosDisplay: created (WPEPlatform, SHM)");
	return WPE_DISPLAY(display);
}
