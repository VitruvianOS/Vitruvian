/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Host-side selection tests for libbacklight against a fake sysfs tree.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <drm.h>
#include "libbacklight.h"

static int g_failures = 0;

static void
expect(int condition, const char* what)
{
	if (condition) {
		printf("ok   %s\n", what);
	} else {
		printf("FAIL %s\n", what);
		g_failures++;
	}
}

static int
write_file(const char* path, const char* contents)
{
	FILE* f = fopen(path, "w");
	if (f == NULL)
		return -1;
	fputs(contents, f);
	fclose(f);
	return 0;
}

static int
make_dir(const char* path)
{
	return mkdir(path, 0755);
}

static int
make_link(const char* target, const char* linkpath)
{
	unlink(linkpath);
	return symlink(target, linkpath);
}

static int
make_node(const char* classDir, const char* name, const char* type,
	const char* maxB, const char* actual, const char* bright,
	const char* deviceTarget)
{
	char path[512];
	if (make_dir(classDir) < 0 && errno != EEXIST)
		return -1;
	snprintf(path, sizeof(path), "%s/%s", classDir, name);
	if (make_dir(path) < 0 && errno != EEXIST)
		return -1;

	snprintf(path, sizeof(path), "%s/%s/type", classDir, name);
	if (write_file(path, type) < 0)
		return -1;
	snprintf(path, sizeof(path), "%s/%s/max_brightness", classDir, name);
	if (write_file(path, maxB) < 0)
		return -1;
	if (actual != NULL) {
		snprintf(path, sizeof(path), "%s/%s/actual_brightness",
			classDir, name);
		if (write_file(path, actual) < 0)
			return -1;
	}
	snprintf(path, sizeof(path), "%s/%s/brightness", classDir, name);
	if (write_file(path, bright) < 0)
		return -1;
	if (deviceTarget != NULL) {
		snprintf(path, sizeof(path), "%s/%s/device", classDir, name);
		if (make_link(deviceTarget, path) < 0)
			return -1;
	}
	return 0;
}

static void
rm_rf(const char* path)
{
	char cmd[600];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
	int rc = system(cmd);
	(void)rc;
}

static void
test_amdgpu_raw_wins_over_firmware(void)
{
	const char* dir = "test-sysfs-kasumi";
	rm_rf(dir);
	// Firmware node first in directory order is not guaranteed; both exist.
	make_node(dir, "amdgpu_bl0", "raw\n", "255\n", "0\n", "0\n",
		"../../0000:00:01.0");
	make_node(dir, "acpi_video0", "firmware\n", "15\n", "0\n", "0\n",
		"../../LNXSYSTM:00");

	struct backlight* bl = backlight_init_from_class(dir, "0000:00:01.0",
		DRM_MODE_CONNECTOR_eDP);
	expect(bl != NULL, "kasumi eDP finds a backlight");
	if (bl != NULL) {
		expect(strstr(bl->path, "amdgpu_bl0") != NULL,
			"kasumi eDP picks PCI-matching amdgpu_bl0 over firmware");
		expect(bl->max_brightness == 255, "amdgpu_bl0 max is 255");
		expect(backlight_set_brightness(bl, 200) == 0,
			"set brightness on amdgpu_bl0 succeeds");
		backlight_destroy(bl);
	}
	rm_rf(dir);
}

static void
test_firmware_used_when_no_gpu_node(void)
{
	const char* dir = "test-sysfs-firmware-only";
	rm_rf(dir);
	make_node(dir, "acpi_video0", "firmware\n", "15\n", "3\n", "3\n",
		"../../LNXSYSTM:00");

	struct backlight* bl = backlight_init_from_class(dir, "0000:00:01.0",
		DRM_MODE_CONNECTOR_eDP);
	expect(bl != NULL, "firmware-only eDP finds a backlight");
	if (bl != NULL) {
		expect(strstr(bl->path, "acpi_video0") != NULL,
			"firmware-only eDP keeps acpi_video0");
		backlight_destroy(bl);
	}
	rm_rf(dir);
}

static void
test_platform_without_device_symlink(void)
{
	const char* dir = "test-sysfs-platform-nolink";
	rm_rf(dir);
	make_node(dir, "panel_bl", "platform\n", "100\n", NULL, "10\n", NULL);

	struct backlight* bl = backlight_init_from_class(dir, "0000:00:01.0",
		DRM_MODE_CONNECTOR_eDP);
	expect(bl != NULL, "platform node without device symlink is eligible");
	if (bl != NULL) {
		expect(strstr(bl->path, "panel_bl") != NULL,
			"platform node selected when it is the only candidate");
		backlight_destroy(bl);
	}
	rm_rf(dir);
}

static void
test_actual_brightness_optional(void)
{
	const char* dir = "test-sysfs-no-actual";
	rm_rf(dir);
	make_node(dir, "amdgpu_bl0", "raw\n", "255\n", NULL, "0\n",
		"../../0000:00:01.0");

	struct backlight* bl = backlight_init_from_class(dir, "0000:00:01.0",
		DRM_MODE_CONNECTOR_eDP);
	expect(bl != NULL, "node without actual_brightness still inits");
	if (bl != NULL) {
		expect(backlight_set_brightness(bl, 128) == 0,
			"set works without actual_brightness");
		backlight_destroy(bl);
	}
	rm_rf(dir);
}

static void
test_external_rejects_platform(void)
{
	const char* dir = "test-sysfs-external";
	rm_rf(dir);
	make_node(dir, "panel_bl", "platform\n", "100\n", NULL, "10\n", NULL);
	make_node(dir, "amdgpu_bl0", "raw\n", "255\n", "10\n", "10\n",
		"../../0000:00:01.0");

	struct backlight* bl = backlight_init_from_class(dir, "0000:00:01.0",
		DRM_MODE_CONNECTOR_HDMIA);
	expect(bl != NULL, "external connector still finds raw node");
	if (bl != NULL) {
		expect(strstr(bl->path, "amdgpu_bl0") != NULL,
			"external connector uses PCI-matching raw only");
		backlight_destroy(bl);
	}
	rm_rf(dir);
}

int
main(void)
{
	test_amdgpu_raw_wins_over_firmware();
	test_firmware_used_when_no_gpu_node();
	test_platform_without_device_symlink();
	test_actual_brightness_optional();
	test_external_rejects_platform();

	if (g_failures != 0) {
		printf("%d failure(s)\n", g_failures);
		return 1;
	}
	printf("all backlight selection tests passed\n");
	return 0;
}
