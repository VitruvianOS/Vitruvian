/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "WebEngine.h"

#include "logging.h"
#include "url_scheme_adapter.h"

#include <private/web/WebProfile.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <unistd.h>


static bool sInitialised = false;
static WebKitWebContext* sContext = NULL;
// The scheme handlers keep this pointer for the life of the context.
static UrlSchemeAdapter* sSchemes = NULL;


// Per-process cap in MB. The thresholds are fractions of it, so it has to
// sit well under physical memory for pressure to ever trigger.
static guint
memory_limit_mb(int profile)
{
	if (profile == BPrivate::B_WEB_PROFILE_MINIMAL)
		return 384;

	long pages = sysconf(_SC_PHYS_PAGES);
	long pageSize = sysconf(_SC_PAGESIZE);
	uint64_t ramMB = 4096;
	if (pages > 0 && pageSize > 0)
		ramMB = (uint64_t)pages * pageSize / (1024 * 1024);
	return (guint)std::min<uint64_t>(ramMB / 4, 1024);
}


WebKitMemoryPressureSettings*
WebEnginePressureSettings(int profile)
{
	WebKitMemoryPressureSettings* pressure
		= webkit_memory_pressure_settings_new();
	webkit_memory_pressure_settings_set_memory_limit(pressure,
		memory_limit_mb(profile));
	// The conservative threshold must stay below the strict one, so the
	// strict one is raised first.
	webkit_memory_pressure_settings_set_strict_threshold(pressure, 0.85);
	webkit_memory_pressure_settings_set_conservative_threshold(pressure, 0.7);
	return pressure;
}


void
WebEngineInit(int profile)
{
	if (sInitialised)
		return;
	sInitialised = true;

	WebKitMemoryPressureSettings* pressure = WebEnginePressureSettings(profile);
	webkit_network_session_set_memory_pressure_settings(pressure);
	webkit_memory_pressure_settings_free(pressure);
}


WebKitWebContext*
WebEngineContext(int profile)
{
	if (sContext == NULL) {
		WebEngineInit(profile);

		// The SHM path renders in software with a large footprint; the
		// default thresholds released memory mid-video and deadlocked
		// the compositor.
		WebKitMemoryPressureSettings* pressure = WebEnginePressureSettings(profile);
		sContext = WEBKIT_WEB_CONTEXT(g_object_new(WEBKIT_TYPE_WEB_CONTEXT,
			"memory-pressure-settings", pressure, NULL));
		webkit_memory_pressure_settings_free(pressure);

		webkit_web_context_set_cache_model(sContext,
			profile == BPrivate::B_WEB_PROFILE_MINIMAL
				? WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER
				: WEBKIT_CACHE_MODEL_WEB_BROWSER);

		if (const char* runtimeDir = g_getenv("XDG_RUNTIME_DIR")) {
			std::string socket = std::string(runtimeDir) + "/pipewire-0";
			webkit_web_context_add_path_to_sandbox(sContext, socket.c_str(),
				FALSE);
		}

		sSchemes = new UrlSchemeAdapter;
		sSchemes->RegisterSchemes(sContext);
		log_info("WebEngine: shared context created");
	}
	return (WebKitWebContext*)g_object_ref(sContext);
}


void
WebEngineApplySettings(WebKitSettings* settings, int profile)
{
	webkit_settings_set_enable_page_cache(settings, FALSE);
	if (profile != BPrivate::B_WEB_PROFILE_MINIMAL)
		return;

	webkit_settings_set_enable_webrtc(settings, FALSE);
	webkit_settings_set_enable_media_stream(settings, FALSE);
	webkit_settings_set_enable_mediasource(settings, FALSE);
	webkit_settings_set_enable_encrypted_media(settings, FALSE);
	webkit_settings_set_enable_webgl(settings, FALSE);
	webkit_settings_set_enable_webaudio(settings, FALSE);
}
