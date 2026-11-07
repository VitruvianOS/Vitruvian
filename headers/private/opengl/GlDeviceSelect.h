/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Shared GPU selection for the GL renderer add-ons: policy over a
 * device table grouped by drmGetDevices2, same ranking idea as
 * DrmDeviceSelect.h (internal panel, connected, boot_vga, first).
 * The pure helpers here are host-testable with synthetic tables.
 */
#ifndef _GL_DEVICE_SELECT_H_
#define _GL_DEVICE_SELECT_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define GL_DEVICE_MAX_PATH	64
#define GL_DEVICE_MAX_DRIVER	32
#define GL_DEVICE_MAX_QUIRKS	16


// One GPU as grouped by drmGetDevices2: render node plus primary node
// plus the sysfs facts policy ranks on. Empty strings mark "not there".
typedef struct gl_device {
	char renderNode[GL_DEVICE_MAX_PATH];
	char primaryNode[GL_DEVICE_MAX_PATH];
	char busId[24];			// "0000:01:00.0"; empty if not PCI
	char driver[GL_DEVICE_MAX_DRIVER];
	uint32_t vendorId;
	uint32_t deviceId;
	bool bootVga;
	bool hasInternal;		// eDP/LVDS/DSI connector present
	bool hasConnected;		// a connector reports "connected"
	bool hasConnectors;
	bool runtimeSuspended;		// power/runtime_status, read not woken
} gl_device;


// Preflet policy (~/config/settings/opengl/gpu_policy).
enum {
	kGlPolicyAutomatic = 0,		// display GPU (the default)
	kGlPolicyPowerSaving,		// display GPU, stated explicitly
	kGlPolicyHighPerformance	// discrete GPU, else display GPU
};


// Why the GL stack runs the way it does; published for the preflet.
enum {
	kGlZinkNative = 0,		// native driver, no zink involved
	kGlZinkQuirk,			// quirk table forces zink
	kGlZinkFallback,		// native failed once, zink retried
	kGlZinkEnv			// VOS_GL_ZINK=1 forced zink
};


// One quirk-table row: force zink for vendor with device ids in range.
typedef struct gl_zink_quirk {
	uint32_t vendorId;
	uint32_t deviceMin;
	uint32_t deviceMax;
	char driver[GL_DEVICE_MAX_DRIVER];	// empty matches any driver
} gl_zink_quirk;


// Rank a device for driving a display; mirrors drm_card_display_rank.
static inline int
gl_device_display_rank(const gl_device* device)
{
	if (device->bootVga && device->hasInternal)
		return 4;
	if (device->hasConnected && device->hasInternal)
		return 3;
	if (device->hasConnected && device->hasConnectors)
		return 2;
	if (device->bootVga && device->hasConnectors)
		return 1;
	if (device->hasConnectors)
		return 0;
	return -1;
}


// A discrete GPU: NVIDIA anywhere, or anything that is neither the boot
// VGA device nor owns an internal panel (AMD/Intel dGPUs, v3d on the Pi).
static inline bool
gl_device_is_discrete(const gl_device* device)
{
	if (device->vendorId == 0x10de)
		return true;
	return !device->bootVga && !device->hasInternal
		&& device->primaryNode[0] != '\0';
}


static inline bool
gl_device_matches_override(const gl_device* device, const char* override)
{
	if (override == NULL || override[0] == '\0')
		return false;
	return strcmp(device->renderNode, override) == 0
		|| (device->primaryNode[0] != '\0'
			&& strcmp(device->primaryNode, override) == 0)
		|| (device->busId[0] != '\0'
			&& strcmp(device->busId, override) == 0)
		|| (device->driver[0] != '\0'
			&& strcmp(device->driver, override) == 0);
}


// Picks the device to run GL on; 'override' (VOS_GL_DEVICE) wins over 'policy'.
// Returns an index into 'devices', or -1 when empty.
static inline int
gl_select_device(const gl_device* devices, int count, int policy,
	const char* override, const char** why)
{
	int i;
	int pick = -1;

	if (count <= 0) {
		if (why != NULL)
			*why = "no GPU found";
		return -1;
	}

	if (override != NULL && override[0] != '\0') {
		for (i = 0; i < count; i++) {
			if (gl_device_matches_override(&devices[i], override)) {
				if (why != NULL)
					*why = "VOS_GL_DEVICE override";
				return i;
			}
		}
	}

	if (policy == kGlPolicyHighPerformance) {
		int suspended = -1;
		for (i = 0; i < count; i++) {
			if (!gl_device_is_discrete(&devices[i]))
				continue;
			if (!devices[i].runtimeSuspended) {
				pick = i;
				break;
			}
			if (suspended < 0)
				suspended = i;
		}
		if (pick < 0 && suspended >= 0)
			pick = suspended;
		if (pick >= 0) {
			if (why != NULL) {
				*why = devices[pick].runtimeSuspended
					? "high performance discrete GPU, runtime suspended"
					: "high performance discrete GPU";
			}
			return pick;
		}
		// No discrete GPU: fall through to the display GPU.
	}

	for (i = 0; i < count; i++) {
		int rank = gl_device_display_rank(&devices[i]);
		int best = pick >= 0
			? gl_device_display_rank(&devices[pick]) : -2;
		if (rank > best
			|| (rank == best && pick >= 0 && devices[i].bootVga
				&& !devices[pick].bootVga))
			pick = i;
	}
	if (pick < 0)
		pick = 0;

	if (why != NULL) {
		const gl_device* device = &devices[pick];
		if (device->bootVga && device->hasInternal)
			*why = "boot_vga internal panel";
		else if (device->hasConnected && device->hasInternal)
			*why = "connected internal panel";
		else if (device->hasConnected)
			*why = "connected connector";
		else if (device->bootVga)
			*why = "boot_vga";
		else
			*why = "first usable device";
	}
	return pick;
}


// One parsed quirk-table row; returns false for comment/blank/junk.
static inline bool
gl_zink_quirk_parse(const char* line, gl_zink_quirk* quirk)
{
	unsigned int vendor = 0, deviceMin = 0, deviceMax = 0;
	char driver[GL_DEVICE_MAX_DRIVER];
	int fields;

	driver[0] = '\0';
	if (line == NULL || line[0] == '\0' || line[0] == '#')
		return false;

	fields = sscanf(line, "%x %x %x %31s", &vendor, &deviceMin,
		&deviceMax, driver);
	if (fields < 3)
		return false;

	quirk->vendorId = vendor;
	quirk->deviceMin = deviceMin;
	quirk->deviceMax = deviceMax;
	strncpy(quirk->driver, driver, sizeof(quirk->driver) - 1);
	quirk->driver[sizeof(quirk->driver) - 1] = '\0';
	return true;
}


static inline bool
gl_zink_quirk_matches(const gl_zink_quirk* quirks, int count,
	uint32_t vendorId, uint32_t deviceId, const char* driver)
{
	int i;

	for (i = 0; i < count; i++) {
		if (quirks[i].vendorId != vendorId)
			continue;
		if (deviceId < quirks[i].deviceMin
				|| deviceId > quirks[i].deviceMax)
			continue;
		if (quirks[i].driver[0] != '\0' && driver != NULL
				&& strcmp(quirks[i].driver, driver) != 0)
			continue;
		return true;
	}
	return false;
}


// Native versus zink for one device. VOS_GL_ZINK: "1" forces, "0" vetoes.
// A quirk hit forces zink; a failed native attempt retries with zink once.
static inline bool
gl_decide_zink(const char* zinkEnv, const gl_zink_quirk* quirks,
	int quirkCount, uint32_t vendorId, uint32_t deviceId,
	const char* driver, bool nativeFailed, int* mode, const char** why)
{
	if (zinkEnv != NULL && strcmp(zinkEnv, "1") == 0) {
		if (mode != NULL)
			*mode = kGlZinkEnv;
		if (why != NULL)
			*why = "env override";
		return true;
	}
	if (zinkEnv != NULL && strcmp(zinkEnv, "0") == 0) {
		if (mode != NULL)
			*mode = kGlZinkNative;
		if (why != NULL)
			*why = "native";
		return false;
	}
	if (gl_zink_quirk_matches(quirks, quirkCount, vendorId, deviceId,
			driver)) {
		if (mode != NULL)
			*mode = kGlZinkQuirk;
		if (why != NULL)
			*why = "zink quirk";
		return true;
	}
	if (nativeFailed) {
		if (mode != NULL)
			*mode = kGlZinkFallback;
		if (why != NULL)
			*why = "zink fallback";
		return true;
	}
	if (mode != NULL)
		*mode = kGlZinkNative;
	if (why != NULL)
		*why = "native";
	return false;
}


// Enumeration and user settings live in GlDeviceSelect.cpp (libdrm,
// sysfs, Be file APIs); returns counts/status codes (0 == B_OK).
// Pure policy above stays header-only so host tests need no runtime.
#ifdef __cplusplus
extern "C" {
#endif
int gl_enumerate_devices(gl_device* devices, int maxDevices);
int gl_load_zink_quirk_table(gl_zink_quirk* quirks, int maxQuirks);

// Preflet setting: ~/config/settings/opengl/gpu_policy, one word.
int gl_load_policy(int* policy);
int gl_save_policy(int policy);

// Renderer-published per-GPU status, read by the preflet.
void gl_publish_gl_status(const gl_device* device, const char* glRenderer,
	int zinkMode, const char* zinkWhy, const char* deviceWhy);
bool gl_read_gl_status(const char* renderNode, char* buffer, size_t size);
#ifdef __cplusplus
}
#endif

#endif // _GL_DEVICE_SELECT_H_
