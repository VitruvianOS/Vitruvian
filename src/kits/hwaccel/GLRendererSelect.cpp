/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT license.
 */

// Smart renderer/backend selection engine. See
// headers/os/hwaccel/GLRendererSelect.h for the settings contract this
// implements; the pure scoring rules live in GLRendererSelectScore.*

#include <GLRendererPriority.h>
#include <GLRendererSelect.h>

#include "GLRendererSelectScore.h"

#include <Autolock.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <GLRenderer.h>
#include <Message.h>
#include <Path.h>
#include <String.h>
#include <StringList.h>

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/utsname.h>
#include <unistd.h>


namespace GLRendererSelect {

const char* kSelectionPath = "opengl/renderer_selection";

const int32 kSettingsSchema = 1;

const char* kSchemaKey = "schema";
const char* kStampKey = "stamp";
const char* kWinnerKey = "winner";
const char* kReasonKey = "reason";
const char* kRecommendedKey = "recommended";
const char* kTableKey = "table";
const char* kOrderKey = "order";
const char* kOverrideKey = "override";

const char* kStampPrefix = "vsos-fp1";


typedef BGLRenderer* (*renderer_factory_t)(BGLView* view, ulong options,
	BGLDispatcher* dispatcher);


static BLocker sLock;
static bool sMemoValid = false;
static Selection sMemo;


static void
_AppendNames(const char* directory, const char* prefix, BString& list)
{
	DIR* dir = opendir(directory);
	if (dir == NULL)
		return;

	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		if (strncmp(entry->d_name, prefix, strlen(prefix)) != 0)
			continue;
		if (list.Length() > 0)
			list << ",";
		list << entry->d_name;
	}

	closedir(dir);
}


// Device existence for the DRM/GPU side of the probe: any /dev/dri node,
// any veno display node, or a janus-inherited DRM fd. Documented in the
// header: this is a structural check, the mini-create is the real gate.
static bool
_HasDrmDevice()
{
	const char* janusFd = getenv("JANUS_DRM_FD");
	if (janusFd != NULL && janusFd[0] != '\0')
		return true;

	DIR* dir = opendir("/dev/dri");
	if (dir == NULL)
		return false;

	bool found = false;
	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		if (strncmp(entry->d_name, "renderD", 7) == 0
			|| strncmp(entry->d_name, "card", 4) == 0) {
			found = true;
			break;
		}
	}

	closedir(dir);
	return found;
}


// zink needs a Vulkan ICD to even load: any driver manifest in the
// standard locations, VK_ICD_FILENAMES, or a loadable loader library.
static bool
_HasVulkanIcd()
{
	const char* env = getenv("VK_ICD_FILENAMES");
	if (env != NULL && env[0] != '\0')
		return true;

	static const char* kIcdDirs[] = {
		"/usr/share/vulkan/icd.d",
		"/etc/vulkan/icd.d",
		NULL,
	};

	for (int i = 0; kIcdDirs[i] != NULL; i++) {
		DIR* dir = opendir(kIcdDirs[i]);
		if (dir == NULL)
			continue;

		struct dirent* entry;
		while ((entry = readdir(dir)) != NULL) {
			size_t length = strlen(entry->d_name);
			if (length > 5 && strcmp(entry->d_name + length - 5,
					".json") == 0) {
				closedir(dir);
				return true;
			}
		}

		closedir(dir);
	}

	const char* home = getenv("HOME");
	if (home != NULL && home[0] != '\0') {
		char path[PATH_MAX];
		snprintf(path, sizeof(path), "%s/.local/share/vulkan/icd.d",
			home);
		if (access(path, R_OK | X_OK) == 0)
			return true;
	}

	return false;
}


static bool
_ReadHexId(const char* path, unsigned int* value)
{
	FILE* file = fopen(path, "r");
	if (file == NULL)
		return false;

	unsigned int parsed = 0;
	bool ok = fscanf(file, "%x", &parsed) == 1;
	fclose(file);

	if (!ok)
		return false;
	*value = parsed;
	return true;
}


// Primary GPU PCI ids (class 0x03, display controller), lower-case hex
// pairs joined by ','. Absent /sys or no display device yields "-".
static void
_GpuPciIds(BString& ids)
{
	DIR* dir = opendir("/sys/bus/pci/devices");
	if (dir == NULL) {
		ids = "-";
		return;
	}

	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.')
			continue;

		char classPath[PATH_MAX];
		snprintf(classPath, sizeof(classPath),
			"/sys/bus/pci/devices/%s/class", entry->d_name);
		unsigned int deviceClass = 0;
		if (!_ReadHexId(classPath, &deviceClass))
			continue;
		if ((deviceClass >> 16) != 0x03)
			continue;

		char vendorPath[PATH_MAX];
		char devicePath[PATH_MAX];
		snprintf(vendorPath, sizeof(vendorPath),
			"/sys/bus/pci/devices/%s/vendor", entry->d_name);
		snprintf(devicePath, sizeof(devicePath),
			"/sys/bus/pci/devices/%s/device", entry->d_name);

		unsigned int vendor = 0;
		unsigned int device = 0;
		if (!_ReadHexId(vendorPath, &vendor)
			|| !_ReadHexId(devicePath, &device)) {
			continue;
		}

		if (ids.Length() > 0)
			ids << ",";
		char pair[16];
		snprintf(pair, sizeof(pair), "%04x:%04x", vendor, device);
		ids << pair;
	}

	closedir(dir);

	if (ids.Length() == 0)
		ids = "-";
}


// The documented fingerprint format:
//   vsos-fp1|<machine>|drm=<a,b>|veno=<a,b>|pci=<vvvv:dddd,...>
static void
_ComputeStamp(BString& stamp)
{
	struct utsname info;
	const char* machine = "unknown";
	if (uname(&info) == 0)
		machine = info.machine;

	BString drm;
	_AppendNames("/dev/dri", "renderD", drm);
	if (drm.Length() == 0)
		_AppendNames("/dev/dri", "card", drm);
	if (drm.Length() == 0)
		drm = "-";

	BString veno;
	_AppendNames("/dev", "veno", veno);
	if (veno.Length() == 0)
		veno = "-";

	BString pci;
	_GpuPciIds(pci);

	stamp = kStampPrefix;
	stamp << "|";
	stamp << machine;
	stamp << "|drm=";
	stamp << drm;
	stamp << "|veno=";
	stamp << veno;
	stamp << "|pci=";
	stamp << pci;
}


enum kind {
	kKindGeneric,
	kKindMesaEgl,
	kKindSurfaceless,
	kKindZink,
};


static kind
_Classify(const char* name)
{
	if (strcmp(name, "mesa_egl") == 0)
		return kKindMesaEgl;
	if (strcmp(name, "mesa_surfaceless") == 0)
		return kKindSurfaceless;
	if (strcmp(name, "zink") == 0)
		return kKindZink;
	return kKindGeneric;
}


// The 1-context mini-create (smoke shape): load the add-on, resolve
// instantiate_gl_renderer, ask for a renderer with no view/window, tear
// it down again. Anything failing short of that disqualifies the add-on.
static void
_ProbeAddOn(const char* path, bool* driverLoads, bool* contextCreates)
{
	*driverLoads = false;
	*contextCreates = false;

	image_id image = load_add_on(path);
	if (image < 0)
		return;

	renderer_factory_t factory = NULL;
	status_t status = get_image_symbol(image, "instantiate_gl_renderer",
		B_SYMBOL_TYPE_TEXT, (void**)&factory);
	if (status != B_OK || factory == NULL) {
		unload_add_on(image);
		return;
	}

	*driverLoads = true;

	BGLRenderer* renderer = factory(NULL, 0, NULL);
	if (renderer != NULL) {
		*contextCreates = true;
		// Fresh object, refcount 1, owned by us: the add-on's operator
		// new pairs with the in-tree destructor via the vtable.
		delete renderer;
	}

	unload_add_on(image);
}


static void
_JoinPins(const BStringList& pins, BString& joined)
{
	joined = "";
	for (int32 i = 0; i < pins.CountStrings(); i++) {
		if (i > 0)
			joined << ",";
		joined << pins.StringAt(i);
	}
}


static bool
_LoadCache(const char* stamp, const char* overrideSnapshot, Selection* out)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return false;
	path.Append(kSelectionPath);

	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return false;

	BMessage message;
	if (message.Unflatten(&file) != B_OK)
		return false;

	int32 schema = 0;
	if (message.FindInt32(kSchemaKey, &schema) != B_OK
		|| schema != kSettingsSchema) {
		return false;
	}

	const char* cachedStamp = NULL;
	if (message.FindString(kStampKey, &cachedStamp) != B_OK
		|| cachedStamp == NULL || strcmp(cachedStamp, stamp) != 0) {
		return false;
	}

	const char* cachedOverride = NULL;
	if (message.FindString(kOverrideKey, &cachedOverride) != B_OK)
		cachedOverride = "";
	if (overrideSnapshot != NULL
		&& strcmp(cachedOverride, overrideSnapshot) != 0) {
		return false;
	}

	const char* winner = NULL;
	if (message.FindString(kWinnerKey, &winner) != B_OK || winner == NULL)
		return false;

	const char* reason = NULL;
	if (message.FindString(kReasonKey, &reason) != B_OK || reason == NULL)
		reason = "cached";

	const char* recommended = NULL;
	if (message.FindString(kRecommendedKey, &recommended) != B_OK)
		recommended = "";

	const char* table = NULL;
	if (message.FindString(kTableKey, &table) != B_OK || table == NULL)
		table = "";

	out->winner = winner;
	out->reason = reason;
	out->recommended = recommended;
	out->table = table;
	out->stamp = cachedStamp;
	out->fromCache = true;

	out->order.MakeEmpty();
	const char* orderName = NULL;
	for (int32 i = 0; message.FindString(kOrderKey, i, &orderName) == B_OK;
			i++) {
		out->order.Add(orderName);
	}

	return true;
}


static void
_SaveCache(const Selection& selection, const char* overrideSnapshot)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;
	path.Append(kSelectionPath);

	BPath parent;
	if (path.GetParent(&parent) == B_OK)
		create_directory(parent.Path(), 0755);

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK)
		return;

	BMessage message;
	message.AddInt32(kSchemaKey, kSettingsSchema);
	message.AddString(kStampKey, selection.stamp.String());
	message.AddString(kWinnerKey, selection.winner.String());
	message.AddString(kReasonKey, selection.reason.String());
	message.AddString(kRecommendedKey, selection.recommended.String());
	message.AddString(kTableKey, selection.table.String());
	message.AddString(kOverrideKey,
		overrideSnapshot != NULL ? overrideSnapshot : "");
	for (int32 i = 0; i < selection.order.CountStrings(); i++)
		message.AddString(kOrderKey, selection.order.StringAt(i).String());

	message.Flatten(&file);
}


status_t
GetSelection(Selection* selection)
{
	if (selection == NULL)
		return B_BAD_VALUE;

	BAutolock lock(&sLock);
	if (sMemoValid) {
		*selection = sMemo;
		return B_OK;
	}

	BString stamp;
	_ComputeStamp(stamp);

	BStringList pins;
	if (GLRendererPriority::Load(pins) != B_OK)
		pins.MakeEmpty();

	BString overrideSnapshot;
	_JoinPins(pins, overrideSnapshot);

	// Cache hit: same machine (stamp) and same pin list. Skip probing.
	if (_LoadCache(stamp.String(), overrideSnapshot.String(), &sMemo)) {
		sMemoValid = true;
		*selection = sMemo;
		return B_OK;
	}

	BStringList installed;
	if (GLRendererPriority::GetInstalledRenderers(installed) != B_OK)
		installed.MakeEmpty();

	using namespace GLRendererSelectScore;

	Candidate candidates[kMaxCandidates];
	int count = 0;
	int installedCount = installed.CountStrings();
	if (installedCount > kMaxCandidates)
		installedCount = kMaxCandidates;

	bool archAmd64 =
#if defined(__x86_64__)
		true;
#else
		false;
#endif

	BString pciIds;
	_GpuPciIds(pciIds);
	unsigned int gpuVendor = 0;
	unsigned int gpuDevice = 0;
	if (pciIds != "-") {
		unsigned int vendor = 0;
		unsigned int device = 0;
		if (sscanf(pciIds.String(), "%x:%x", &vendor, &device) == 2) {
			gpuVendor = vendor;
			gpuDevice = device;
		}
	}

	for (int32 i = 0; i < installedCount; i++) {
		BString name = installed.StringAt(i);

		Candidate& candidate = candidates[count];
		memset(&candidate, 0, sizeof(candidate));
		snprintf(candidate.name, sizeof(candidate.name), "%s",
			name.String());
		candidate.registrationIndex = count;
		candidate.installed = true;
		candidate.archAmd64 = archAmd64;
		candidate.vendorId = gpuVendor;
		candidate.deviceId = gpuDevice;

		kind candidateKind = _Classify(name.String());
		candidate.zink = candidateKind == kKindZink;

		bool needsDevice = candidateKind == kKindMesaEgl;
		bool hasIcd = candidateKind != kKindZink || _HasVulkanIcd();
		candidate.devicePresent = hasIcd
			&& (needsDevice ? _HasDrmDevice() : true);

		if (candidate.devicePresent) {
			char path[PATH_MAX];
			snprintf(path, sizeof(path), "/system/add-ons/opengl/%s",
				name.String());
			_ProbeAddOn(path, &candidate.driverLoads,
				&candidate.contextCreates);
		}

		// A mesa_surfaceless context counts as hardware only when a DRM node
		// exists, otherwise it is the llvmpipe software path.
		candidate.hardware = candidate.devicePresent;
		if (candidateKind == kKindSurfaceless)
			candidate.hardware = _HasDrmDevice();

		count++;
	}

	const char* pinNames[kMaxCandidates];
	int pinCount = 0;
	for (int32 i = 0; i < pins.CountStrings() && pinCount < kMaxCandidates;
			i++) {
		pinNames[pinCount++] = pins.StringAt(i).String();
	}

	Result result;
	Score(candidates, count,
		pinCount > 0 ? pinNames : NULL, pinCount, &result);

	char line[kMaxLine];
	FormatSelectionLine(&result, candidates, count, stamp.String(), line,
		sizeof(line));
	// syslog convention: the winner + scoring table in ONE tree line.
	syslog(LOG_INFO, "%s", line);

	if (result.winner >= 0)
		sMemo.winner = candidates[result.winner].name;
	else
		sMemo.winner = "";
	sMemo.reason = result.winnerReason;
	if (result.recommended >= 0)
		sMemo.recommended = candidates[result.recommended].name;
	else
		sMemo.recommended = "";
	sMemo.table = result.table;
	sMemo.stamp = stamp;
	sMemo.fromCache = false;
	sMemo.order.MakeEmpty();
	for (int i = 0; i < result.orderCount; i++)
		sMemo.order.Add(candidates[result.order[i]].name);

	_SaveCache(sMemo, overrideSnapshot.String());

	sMemoValid = true;
	*selection = sMemo;
	return B_OK;
}

} // namespace GLRendererSelect
