/*
 * libbacklight - userspace interface to Linux backlight control
 *
 * Copyright © 2012 Intel Corporation
 * Copyright 2010 Red Hat <mjg@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Matthew Garrett <mjg@redhat.com>
 *    Tiago Vignatti <vignatti@freedesktop.org>
 */

#define _GNU_SOURCE

#include "libbacklight.h"
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>
#include <linux/types.h>
#include <dirent.h>
#include <drm.h>
#include <fcntl.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>

static long backlight_get(struct backlight *backlight, char *node)
{
	char buffer[100];
	char *path;
	int fd;
	long value, ret;

	if (asprintf(&path, "%s/%s", backlight->path, node) < 0)
		return -ENOMEM;
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		ret = -1;
		goto out;
	}

	ret = read(fd, &buffer, sizeof(buffer));
	if (ret < 1) {
		ret = -1;
		goto out;
	}

	value = strtol(buffer, NULL, 10);
	ret = value;
out:
	if (fd >= 0)
		close(fd);
	free(path);
	return ret;
}

long backlight_get_brightness(struct backlight *backlight)
{
	return backlight_get(backlight, "brightness");
}

long backlight_get_max_brightness(struct backlight *backlight)
{
	return backlight_get(backlight, "max_brightness");
}

long backlight_get_actual_brightness(struct backlight *backlight)
{
	return backlight_get(backlight, "actual_brightness");
}

long backlight_set_brightness(struct backlight *backlight, long brightness)
{
	char *path;
	char *buffer = NULL;
	int fd = -1;
	long ret;

	if (asprintf(&path, "%s/%s", backlight->path, "brightness") < 0)
		return -ENOMEM;

	// sysfs brightness may be write-only; a read-first would skip the write
	// and leave the panel dark. Return 0 on success, matching callers.
	fd = open(path, O_WRONLY);
	if (fd < 0) {
		ret = -1;
		goto out;
	}

	if (asprintf(&buffer, "%ld", brightness) < 0) {
		ret = -1;
		goto out;
	}

	ret = write(fd, buffer, strlen(buffer));
	if (ret < 0) {
		ret = -1;
		goto out;
	}

	backlight->brightness = brightness;
	ret = 0;
out:
	free(buffer);
	free(path);
	if (fd >= 0)
		close(fd);
	return ret;
}

void backlight_destroy(struct backlight *backlight)
{
	if (!backlight)
		return;

	if (backlight->path)
		free(backlight->path);

	free(backlight);
}

// Higher wins. A PCI-matching node is the GPU's own panel backlight
// (amdgpu_bl0 on AMD Chromebooks); it must beat non-matching firmware
// or platform nodes or the session writes the wrong device.
static int
backlight_entry_score(enum backlight_type type, int pciMatch,
	int internalConnector)
{
	if (pciMatch)
		return 100 + (int)type;
	if (!internalConnector)
		return -1;
	return (int)type;
}

struct backlight *
backlight_init_from_class(const char* class_dir, const char* pci_name,
	uint32_t connector_type)
{
	char *chosen_path = NULL;
	char *path = NULL;
	DIR *backlights = NULL;
	struct dirent *entry;
	enum backlight_type type = 0;
	int bestScore = -1;
	char buffer[100];
	struct backlight *backlight = NULL;
	int ret;

	if (class_dir == NULL || class_dir[0] == '\0')
		return NULL;

	if (connector_type <= 0)
		return NULL;

	int internalConnector = (connector_type == DRM_MODE_CONNECTOR_LVDS
		|| connector_type == DRM_MODE_CONNECTOR_eDP
		|| connector_type == DRM_MODE_CONNECTOR_DSI);

	backlights = opendir(class_dir);
	if (!backlights)
		return NULL;

	/* Internal panels may use platform or firmware nodes; external
	   connectors need raw GPU control. Preference is no longer type-only:
	   a raw node that PCI-matches the display GPU wins on internal panels. */

	while ((entry = readdir(backlights))) {
		char *backlight_path;
		char *parent = NULL;
		enum backlight_type entry_type;
		int score;
		int fd;

		if (entry->d_name[0] == '.')
			continue;

		if (asprintf(&backlight_path, "%s/%s", class_dir,
			     entry->d_name) < 0)
			goto err;

		if (asprintf(&path, "%s/%s", backlight_path, "type") < 0)
			goto err;

		fd = open(path, O_RDONLY);

		if (fd < 0)
			goto out;

		ret = read (fd, &buffer, sizeof(buffer));
		close (fd);

		if (ret < 1)
			goto out;

		buffer[ret] = '\0';

		if (!strncmp(buffer, "raw\n", sizeof(buffer)))
			entry_type = BACKLIGHT_RAW;
		else if (!strncmp(buffer, "platform\n", sizeof(buffer)))
			entry_type = BACKLIGHT_PLATFORM;
		else if (!strncmp(buffer, "firmware\n", sizeof(buffer)))
			entry_type = BACKLIGHT_FIRMWARE;
		else
			goto out;

		if (!internalConnector && entry_type != BACKLIGHT_RAW)
			goto out;

		free (path);
		path = NULL;

		if (asprintf(&path, "%s/%s", backlight_path, "device") < 0)
			goto err;

		ret = readlink(path, buffer, sizeof(buffer) - 1);
		if (ret < 0) {
			// Platform nodes may carry no device symlink.
			if (entry_type != BACKLIGHT_PLATFORM)
				goto out;
			parent = NULL;
		} else {
			buffer[ret] = '\0';
			parent = basename(buffer);
		}

		int pciMatch = 0;
		if (entry_type == BACKLIGHT_RAW) {
			// Raw nodes are GPU-owned; without a PCI match they
			// cannot be this card's panel backlight.
			if (!(pci_name && parent && !strcmp(pci_name, parent)))
				goto out;
			pciMatch = 1;
		} else if (entry_type == BACKLIGHT_FIRMWARE) {
			// Firmware may live outside the GPU (acpi_video0); still
			// a candidate when nothing PCI-matched is present.
			if (pci_name && parent && !strcmp(pci_name, parent))
				pciMatch = 1;
		}

		score = backlight_entry_score(entry_type, pciMatch,
			internalConnector);
		if (score < bestScore)
			goto out;

		type = entry_type;
		bestScore = score;

		if (chosen_path)
			free(chosen_path);
		chosen_path = strdup(backlight_path);

	out:
		free(backlight_path);
		free(path);
		path = NULL;
	}

	if (!chosen_path)
		goto err;

	backlight = malloc(sizeof(struct backlight));

	if (!backlight)
		goto err;

	backlight->path = chosen_path;
	backlight->type = type;

	backlight->max_brightness = backlight_get_max_brightness(backlight);
	if (backlight->max_brightness < 0)
		goto err;

	// actual_brightness is optional; many platform nodes omit it.
	backlight->brightness = backlight_get_actual_brightness(backlight);
	if (backlight->brightness < 0)
		backlight->brightness = backlight_get_brightness(backlight);

	closedir(backlights);
	return backlight;
err:
	closedir(backlights);
	free(path);
	free (chosen_path);
	free (backlight);
	return NULL;
}

struct backlight *backlight_init(struct udev_device *drm_device,
				 uint32_t connector_type)
{
	const char *syspath = NULL;
	char *pci_name = NULL;
	char *path = NULL;
	char buffer[100];
	int ret;

	if (!drm_device)
		return NULL;

	syspath = udev_device_get_syspath(drm_device);
	if (!syspath)
		return NULL;

	if (asprintf(&path, "%s/%s", syspath, "device") < 0)
		return NULL;

	ret = readlink(path, buffer, sizeof(buffer) - 1);
	free(path);
	if (ret < 0)
		return NULL;

	buffer[ret] = '\0';
	pci_name = basename(buffer);

	const char* class_dir = getenv("VOS_BACKLIGHT_CLASS");
	if (class_dir == NULL || class_dir[0] == '\0')
		class_dir = "/sys/class/backlight";

	return backlight_init_from_class(class_dir, pci_name, connector_type);
}
