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
	B_WEB_PROFILE_MINIMAL = 1
};

// Must run before SetSession() and before the view is attached. The first
// view of an app also fixes the shared engine's cache model and memory
// limits.
void	SetWebViewProfile(BWebView* view, web_profile profile);

}	// namespace BPrivate


#endif	// _WEB_PROFILE_H
