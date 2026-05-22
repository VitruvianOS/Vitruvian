/*
 * Radeon multi-monitor tunnel stubs — not supported on this platform.
 * All multimon controls are hidden at runtime when TestMultiMonSupport fails.
 */
#ifndef _MULTIMON_STUB_H
#define _MULTIMON_STUB_H

#include <SupportDefs.h>

class BScreen;

static inline status_t TestMultiMonSupport(BScreen*)               { return B_UNSUPPORTED; }
static inline status_t GetSwapDisplays(BScreen*, bool*)            { return B_UNSUPPORTED; }
static inline status_t SetSwapDisplays(BScreen*, bool)             { return B_UNSUPPORTED; }
static inline status_t GetUseLaptopPanel(BScreen*, bool*)          { return B_UNSUPPORTED; }
static inline status_t SetUseLaptopPanel(BScreen*, bool)           { return B_UNSUPPORTED; }
static inline status_t GetNthSupportedTVStandard(BScreen*, int, uint32*) { return B_UNSUPPORTED; }
static inline status_t GetTVStandard(BScreen*, uint32*)            { return B_UNSUPPORTED; }
static inline status_t SetTVStandard(BScreen*, uint32)             { return B_UNSUPPORTED; }

#endif	// _MULTIMON_STUB_H
