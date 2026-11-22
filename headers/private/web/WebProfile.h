/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */
#ifndef _WEB_PROFILE_H
#define _WEB_PROFILE_H


class BWebView;


namespace BPrivate {

enum web_profile {
	// Full browser: media, WebGL and WebRTC stay as the engine ships them.
	B_WEB_PROFILE_BROWSER = 0,
	// Embedded web app: no WebRTC, media stream, MSE/EME, WebGL, WebAudio,
	// DNS prefetch; small cache.
	B_WEB_PROFILE_MINIMAL = 1,
	// Minimal, but the HTTP disk cache stays on (remote images).
	B_WEB_PROFILE_MINIMAL_CACHED = 2
};

// Must run before SetSession() and before the view is attached. The first
// view of an app also fixes the shared engine's cache model and memory
// limits.
void	SetWebViewProfile(BWebView* view, web_profile profile);

// The URL the view is about to load, set before it is attached. A view
// opened for a site that another live view of the same session shows
// shares that view's web process; without it the view gets its own.
// Browser profile and persistent sessions only.
void	SetWebViewInitialURL(BWebView* view, const char* url);

}	// namespace BPrivate


#endif	// _WEB_PROFILE_H
