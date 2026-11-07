/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * GPU enumeration (drmGetDevices2 + sysfs) and the user settings the
 * preflet writes. The ranking policy itself is pure and lives in
 * GlDeviceSelect.h so host tests can drive it with synthetic tables.
 */

#include "GlDeviceSelect.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <SupportDefs.h>


static const char* kPolicySettingsPath = "opengl/gpu_policy";
static const char* kStatusDirName = "opengl/gl_status";
static const char* kQuirkTablePath = "/system/data/opengl/zink_quirks";


// Read a small sysfs attribute into 'buffer'; false when absent.
static bool
_ReadSysfs(const char* path, char* buffer, size_t size)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return false;

	ssize_t n = read(fd, buffer, size - 1);
	close(fd);
	if (n <= 0)
		return false;
	buffer[n] = '\0';
	while (n > 0 && (buffer[n - 1] == '\n' || buffer[n - 1] == ' '))
		buffer[--n] = '\0';
	return true;
}


// Basename of the driver bound to a DRM node, via sysfs only: opening
// the node just to ask its name could wake a runtime-suspended dGPU.
static void
_ReadDriverName(const char* nodePath, char* driver, size_t size)
{
	char linkPath[128];
	char target[256];
	const char* base;

	driver[0] = '\0';
	if (nodePath == NULL || nodePath[0] == '\0')
		return;

	snprintf(linkPath, sizeof(linkPath), "/sys/class/drm/%s/device/driver",
		strrchr(nodePath, '/') != NULL
			? strrchr(nodePath, '/') + 1 : nodePath);

	ssize_t n = readlink(linkPath, target, sizeof(target) - 1);
	if (n <= 0)
		return;
	target[n] = '\0';

	base = strrchr(target, '/');
	base = base != NULL ? base + 1 : target;
	strncpy(driver, base, size - 1);
	driver[size - 1] = '\0';
}


// eDP/LVDS/DSI means the device owns a laptop panel.
static bool
_ConnectorIsInternal(uint32_t type)
{
	return type == DRM_MODE_CONNECTOR_LVDS
		|| type == DRM_MODE_CONNECTOR_eDP
		|| type == DRM_MODE_CONNECTOR_DSI;
}


// Probe connectors on a card node. Never called for a runtime
// suspended device: that would wake a dGPU the policy may not want.
static void
_ProbeConnectors(const char* primaryNode, gl_device* device)
{
	int fd = open(primaryNode, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		fd = open(primaryNode, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return;

	drmModeRes* res = drmModeGetResources(fd);
	if (res == NULL) {
		close(fd);
		return;
	}

	for (int i = 0; i < res->count_connectors; i++) {
		drmModeConnector* conn = drmModeGetConnector(fd,
			res->connectors[i]);
		if (conn == NULL)
			continue;
		device->hasConnectors = true;
		if (conn->connection == DRM_MODE_CONNECTED)
			device->hasConnected = true;
		if (_ConnectorIsInternal(conn->connector_type))
			device->hasInternal = true;
		drmModeFreeConnector(conn);
	}

	drmModeFreeResources(res);
	close(fd);
}


static bool
_DriverIsFirmware(const char* driver)
{
	return strcmp(driver, "simple-framebuffer") == 0
		|| strcmp(driver, "simpledrm") == 0
		|| strcmp(driver, "efi-framebuffer") == 0
		|| strcmp(driver, "efidrm") == 0;
}


int
gl_enumerate_devices(gl_device* devices, int maxDevices)
{
	drmDevicePtr devs[16];
	int count;

	if (devices == NULL || maxDevices <= 0)
		return -1;

	count = drmGetDevices2(0, devs, 16);
	if (count < 0)
		return -1;

	int filled = 0;
	for (int i = 0; i < count && filled < maxDevices; i++) {
		drmDevice* dev = devs[i];
		gl_device* device = &devices[filled];

		memset(device, 0, sizeof(*device));

		if ((dev->available_nodes & (1 << DRM_NODE_RENDER)) != 0
				&& dev->nodes[DRM_NODE_RENDER] != NULL) {
			strncpy(device->renderNode, dev->nodes[DRM_NODE_RENDER],
				sizeof(device->renderNode) - 1);
		}
		if ((dev->available_nodes & (1 << DRM_NODE_PRIMARY)) != 0
				&& dev->nodes[DRM_NODE_PRIMARY] != NULL) {
			strncpy(device->primaryNode, dev->nodes[DRM_NODE_PRIMARY],
				sizeof(device->primaryNode) - 1);
		}

		// Prefer the render node for sysfs lookups; render-only
		// devices (kmsro) have no primary node at all.
		const char* node = device->renderNode[0] != '\0'
			? device->renderNode : device->primaryNode;

		_ReadDriverName(node, device->driver, sizeof(device->driver));
		if (_DriverIsFirmware(device->driver))
			continue;

		if (dev->bustype == DRM_BUS_PCI && dev->businfo.pci != NULL) {
			snprintf(device->busId, sizeof(device->busId),
				"%04x:%02x:%02x.%x",
				dev->businfo.pci->domain, dev->businfo.pci->bus,
				dev->businfo.pci->dev, dev->businfo.pci->func);
			if (dev->deviceinfo.pci != NULL) {
				device->vendorId = dev->deviceinfo.pci->vendor_id;
				device->deviceId = dev->deviceinfo.pci->device_id;
			}

			char path[128];
			char value[8];

			snprintf(path, sizeof(path),
				"/sys/bus/pci/devices/%s/boot_vga", device->busId);
			if (_ReadSysfs(path, value, sizeof(value)))
				device->bootVga = atoi(value) == 1;

			snprintf(path, sizeof(path),
				"/sys/bus/pci/devices/%s/power/runtime_status",
				device->busId);
			if (_ReadSysfs(path, value, sizeof(value)))
				device->runtimeSuspended =
					strcmp(value, "suspended") == 0;
		}

		// Connector state needs a modeset fd; a suspended device
		// stays dark in the table on purpose.
		if (device->primaryNode[0] != '\0'
				&& !device->runtimeSuspended)
			_ProbeConnectors(device->primaryNode, device);

		filled++;
	}

	drmFreeDevices(devs, count);
	return filled;
}


int
gl_load_zink_quirk_table(gl_zink_quirk* quirks, int maxQuirks)
{
	if (quirks == NULL || maxQuirks <= 0)
		return 0;

	FILE* file = fopen(kQuirkTablePath, "r");
	if (file == NULL)
		return 0;

	int loaded = 0;
	char line[128];
	while (loaded < maxQuirks && fgets(line, sizeof(line), file) != NULL) {
		if (gl_zink_quirk_parse(line, &quirks[loaded]))
			loaded++;
	}

	fclose(file);
	return loaded;
}


status_t
gl_load_policy(int* policy)
{
	if (policy == NULL)
		return B_BAD_VALUE;
	*policy = kGlPolicyAutomatic;

	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;
	path.Append(kPolicySettingsPath);

	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return B_OK;

	char value[32];
	ssize_t n = file.Read(value, sizeof(value) - 1);
	if (n <= 0)
		return B_OK;
	value[n] = '\0';
	while (n > 0 && (value[n - 1] == '\n' || value[n - 1] == ' '))
		value[--n] = '\0';

	if (strcmp(value, "power_saving") == 0)
		*policy = kGlPolicyPowerSaving;
	else if (strcmp(value, "high_performance") == 0)
		*policy = kGlPolicyHighPerformance;
	else
		*policy = kGlPolicyAutomatic;

	return B_OK;
}


status_t
gl_save_policy(int policy)
{
	const char* value = "automatic";
	if (policy == kGlPolicyPowerSaving)
		value = "power_saving";
	else if (policy == kGlPolicyHighPerformance)
		value = "high_performance";

	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;
	path.Append(kPolicySettingsPath);

	BPath parent;
	if (path.GetParent(&parent) == B_OK)
		create_directory(parent.Path(), 0755);

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status = file.InitCheck();
	if (status != B_OK)
		return status;

	size_t length = strlen(value);
	if (file.Write(value, length) != (ssize_t)length)
		return B_ERROR;

	return B_OK;
}


// One status file per render node under gl_status/, written by the
// renderer after context creation, read by the preflet.
void
gl_publish_gl_status(const gl_device* device, const char* glRenderer,
	int /*zinkMode*/, const char* zinkWhy, const char* deviceWhy)
{
	if (device == NULL)
		return;

	const char* node = device->renderNode[0] != '\0'
		? device->renderNode : device->primaryNode;
	if (node[0] == '\0')
		return;

	const char* base = strrchr(node, '/');
	base = base != NULL ? base + 1 : node;

	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;
	path.Append(kStatusDirName);
	create_directory(path.Path(), 0755);
	path.Append(base);

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK)
		return;

	char line[512];
	snprintf(line, sizeof(line), "%s|%s|%s\n",
		glRenderer != NULL ? glRenderer : "?",
		zinkWhy != NULL ? zinkWhy : "?",
		deviceWhy != NULL ? deviceWhy : "?");
	file.Write(line, strlen(line));
}


bool
gl_read_gl_status(const char* renderNode, char* buffer, size_t size)
{
	if (renderNode == NULL || renderNode[0] == '\0' || buffer == NULL)
		return false;

	const char* base = strrchr(renderNode, '/');
	base = base != NULL ? base + 1 : renderNode;

	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return false;
	path.Append(kStatusDirName);
	path.Append(base);

	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return false;

	ssize_t n = file.Read(buffer, size - 1);
	if (n <= 0)
		return false;
	buffer[n] = '\0';
	return true;
}
