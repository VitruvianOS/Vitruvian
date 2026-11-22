/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */
#ifndef VOS_WEB_ENGINE_H
#define VOS_WEB_ENGINE_H


#include <wpe/webkit.h>


// Sets the memory limits of the network process. Must run before the first
// network session exists; later calls do nothing. GLib thread only.
void						WebEngineInit();

// Per-process memory limits; the caller frees the result.
WebKitMemoryPressureSettings*	WebEnginePressureSettings();


#endif // VOS_WEB_ENGINE_H
