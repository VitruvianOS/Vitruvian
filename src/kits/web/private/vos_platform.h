/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */
#ifndef VOS_WEB_PLATFORM_H
#define VOS_WEB_PLATFORM_H


#include <glib-object.h>
#include <wpe/wpe-platform.h>

#include <functional>


struct CompositorAdapter;


// Callbacks the platform objects use to reach the embedder. Every one
// of them runs on the GLib thread (the WebKit UI thread).
struct VosPlatformHost {
	CompositorAdapter*					compositor;
	// The page asked to enter or leave full screen.
	std::function<void(bool)>			onFullscreenRequest;
	// The page asked to resize its window (window.resizeTo).
	std::function<void(int, int)>		onResizeRequest;
	// Cursor shape name from the view ("text", "pointer", ...).
	std::function<void(const char*)>	onCursorName;
};


#define VOS_TYPE_DISPLAY (vos_display_get_type())
G_DECLARE_FINAL_TYPE(VosDisplay, vos_display, VOS, DISPLAY, WPEDisplay)

#define VOS_TYPE_TOPLEVEL (vos_toplevel_get_type())
G_DECLARE_FINAL_TYPE(VosToplevel, vos_toplevel, VOS, TOPLEVEL, WPEToplevel)

#define VOS_TYPE_VIEW (vos_view_get_type())
G_DECLARE_FINAL_TYPE(VosView, vos_view, VOS, VIEW, WPEView)


// The display is returned unconnected: call wpe_display_connect() before
// handing it to a WebKitWebView through its "display" property.
WPEDisplay*		vos_display_new(VosPlatformHost* host);

// The host the next view and toplevel of a shared display belong to;
// GLib thread, set around the creation of a related view.
void			vos_display_set_next_host(VosPlatformHost* host);


#endif // VOS_WEB_PLATFORM_H
