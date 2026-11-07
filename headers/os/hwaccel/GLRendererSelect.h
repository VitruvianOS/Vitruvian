/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT license.
 */
#ifndef HWACCEL_GL_RENDERER_SELECT_H
#define HWACCEL_GL_RENDERER_SELECT_H


#include <String.h>
#include <StringList.h>
#include <SupportDefs.h>


// Picks the GL backend at runtime from installed add-ons, probing device,
// driver load and context create. Settings live under opengl/ in user settings;
// deleting renderer_selection forces a re-probe. Zink is only ever recommended.
namespace GLRendererSelect {

// Selection cache file, relative to B_USER_SETTINGS_DIRECTORY.
extern const char* kSelectionPath;

extern const int32 kSettingsSchema;

// Keys inside the flattened-BMessage cache file.
extern const char* kSchemaKey;
extern const char* kStampKey;
extern const char* kWinnerKey;
extern const char* kReasonKey;
extern const char* kRecommendedKey;
extern const char* kTableKey;
extern const char* kOrderKey;
extern const char* kOverrideKey;

// The device fingerprint ("vsos-fp1"): arch | drm nodes | veno nodes |
// PCI vendor:device pairs of display controllers. Documented format:
//   vsos-fp1|<machine>|drm=<a,b>|veno=<a,b>|pci=<vvvv:dddd,...>
// Subsequent boots skip re-probing while this matches the cached stamp.
extern const char* kStampPrefix;

struct Selection {
	BString		winner;
	BString		reason;
	BString		recommended;	// empty unless a different opt-in won
	BString		table;
	BString		stamp;
	BStringList	order;			// try order, winner first
	bool		fromCache;
};


// First-use probe: enumerate, probe each candidate, score, log one line
// and cache to kSelectionPath. Only the first call per process probes.
// Never fails hard: on error it falls back to the default precedence.
status_t GetSelection(Selection* selection);

} // namespace GLRendererSelect


#endif // HWACCEL_GL_RENDERER_SELECT_H
