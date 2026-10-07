/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef __LADSPA_ABI_H__
#define __LADSPA_ABI_H__

// LADSPA descriptor ABI, declared here the way vst_host declares VST2:
// the ABI is frozen and there is no SDK to ship. Matches ladspa.h.

#include <stdint.h>

typedef float LADSPA_Data;

typedef void* LADSPA_Handle;

typedef enum {
	LADSPA_PORT_INPUT  = 0x1,
	LADSPA_PORT_OUTPUT = 0x2
} LADSPA_PortDescriptor;

typedef enum {
	LADSPA_PORT_CONTROL = 0x4,
	LADSPA_PORT_AUDIO   = 0x8
} LADSPA_PortRangeHintDescriptor;

typedef struct _LADSPA_RangeHintDescriptor {
	unsigned long LowerBoundHint;
	unsigned long UpperBoundHint;
} LADSPA_RangeHintDescriptor;

typedef struct _LADSPA_PortRangeHint {
	unsigned long HintDescriptor;
	LADSPA_Data LowerBound;
	LADSPA_Data UpperBound;
} LADSPA_PortRangeHint;

typedef LADSPA_Handle (*LADSPA_InstantiateFunction)(
	const struct _LADSPA_Descriptor* descriptor,
	unsigned long sampleRate);

typedef void (*LADSPA_ConnectPortFunction)(LADSPA_Handle instance,
	unsigned long port, LADSPA_Data* dataLocation);

typedef void (*LADSPA_ActivateFunction)(LADSPA_Handle instance);

typedef void (*LADSPA_RunFunction)(LADSPA_Handle instance,
	unsigned long sampleCount);

typedef void (*LADSPA_CleanupFunction)(LADSPA_Handle instance);

typedef struct _LADSPA_Descriptor {
	unsigned long UniqueID;
	const char* Label;
	unsigned long Properties;
	const char* Name;
	const char* Maker;
	const char* Copyright;
	unsigned long PortCount;
	const LADSPA_PortDescriptor* PortDescriptors;
	const char* const* PortNames;
	const LADSPA_PortRangeHint* PortRangeHints;
	const void* ImplementationData;
	LADSPA_InstantiateFunction instantiate;
	LADSPA_ConnectPortFunction connect_port;
	LADSPA_ActivateFunction activate;
	LADSPA_RunFunction run;
	LADSPA_RunFunction run_adding;
	LADSPA_ActivateFunction set_run_adding_gain;
	LADSPA_CleanupFunction cleanup;
} LADSPA_Descriptor;

typedef const LADSPA_Descriptor* (*LADSPA_Descriptor_Function)(
	unsigned long index);

#endif //__LADSPA_ABI_H__
