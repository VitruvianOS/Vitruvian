/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "WebEngine.h"

#include <algorithm>
#include <cstdint>
#include <unistd.h>


static bool sInitialised = false;


// Per-process cap in MB. The thresholds are fractions of it, so it has to
// sit well under physical memory for pressure to ever trigger.
static guint
memory_limit_mb()
{
	long pages = sysconf(_SC_PHYS_PAGES);
	long pageSize = sysconf(_SC_PAGESIZE);
	uint64_t ramMB = 4096;
	if (pages > 0 && pageSize > 0)
		ramMB = (uint64_t)pages * pageSize / (1024 * 1024);
	return (guint)std::min<uint64_t>(ramMB / 4, 1024);
}


WebKitMemoryPressureSettings*
WebEnginePressureSettings()
{
	WebKitMemoryPressureSettings* pressure
		= webkit_memory_pressure_settings_new();
	webkit_memory_pressure_settings_set_memory_limit(pressure,
		memory_limit_mb());
	// The conservative threshold must stay below the strict one, so the
	// strict one is raised first.
	webkit_memory_pressure_settings_set_strict_threshold(pressure, 0.85);
	webkit_memory_pressure_settings_set_conservative_threshold(pressure, 0.7);
	return pressure;
}


void
WebEngineInit()
{
	if (sInitialised)
		return;
	sInitialised = true;

	WebKitMemoryPressureSettings* pressure = WebEnginePressureSettings();
	webkit_network_session_set_memory_pressure_settings(pressure);
	webkit_memory_pressure_settings_free(pressure);
}
