/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef _MEDIA2_SYSTEM_BEEP_H
#define _MEDIA2_SYSTEM_BEEP_H


#include <SupportDefs.h>


#ifdef __cplusplus
extern "C" {
#endif

status_t	vos_system_beep_ensure_events(void);
status_t	vos_system_beep_add_event(const char* name, uint32 flags);
status_t	vos_system_beep_play(const char* name);

#ifdef __cplusplus
}
#endif


#endif
