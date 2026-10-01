/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Shared DRM card selection for janus_session and app_server.
 */
#ifndef _DRM_DEVICE_SELECT_H_
#define _DRM_DEVICE_SELECT_H_

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <libseat.h>
#include <xf86drm.h>
#include <xf86drmMode.h>


// True if this DRM fd has any connector, i.e. can drive a display at all;
// render-only GPUs (e.g. the Raspberry Pi's v3d next to vc4) have none.
static inline bool
drm_card_has_connectors(int fd)
{
	drmModeRes* res = drmModeGetResources(fd);
	if (res == NULL)
		return false;
	bool has = res->count_connectors > 0;
	drmModeFreeResources(res);
	return has;
}


// True if any connector on this DRM fd is currently connected.
static inline bool
drm_card_has_connected_connector(int fd)
{
	drmModeRes* res = drmModeGetResources(fd);
	if (res == NULL)
		return false;

	bool connected = false;
	for (int i = 0; i < res->count_connectors; i++) {
		drmModeConnector* conn = drmModeGetConnector(fd,
			res->connectors[i]);
		if (conn == NULL)
			continue;
		if (conn->connection == DRM_MODE_CONNECTED) {
			connected = true;
			drmModeFreeConnector(conn);
			break;
		}
		drmModeFreeConnector(conn);
	}

	drmModeFreeResources(res);
	return connected;
}


// True if the firmware marked this card as the boot display.
static inline bool
drm_card_is_boot_vga(int index)
{
	char path[64];
	snprintf(path, sizeof(path),
		"/sys/class/drm/card%d/device/boot_vga", index);

	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return false;

	char buf[4];
	ssize_t n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return false;
	buf[n] = '\0';

	return atoi(buf) == 1;
}


// Open every /dev/dri/cardN on seat, keep the one that drives a display and close the rest.
// Returns false if nothing opened, leaving the out-params as -1/NULL.
static inline bool
drm_select_seat_device(struct libseat* seat, int& deviceId, int& fd,
	int& cardIndex, const char*& why)
{
	// Optimus laptops expose the NVIDIA card first with no panel, and a Raspberry Pi exposes render-only v3d
	// next to vc4. Prefer a card with a connected display, then one that can drive a display, then the first.
	char path[64];
	int ids[10];
	int fds[10];
	int indexes[10];
	bool connected[10];
	bool hasConnectors[10];
	bool bootVga[10];
	int n = 0;

	for (int i = 0; i <= 9; i++) {
		snprintf(path, sizeof(path), "/dev/dri/card%d", i);
		int cardFd = -1;
		int id = libseat_open_device(seat, path, &cardFd);
		if (id < 0 || cardFd < 0)
			continue;

		ids[n] = id;
		fds[n] = cardFd;
		indexes[n] = i;
		connected[n] = drm_card_has_connected_connector(cardFd);
		hasConnectors[n] = drm_card_has_connectors(cardFd);
		bootVga[n] = drm_card_is_boot_vga(i);
		n++;
	}

	if (n == 0) {
		deviceId = -1;
		fd = -1;
		cardIndex = -1;
		why = NULL;
		return false;
	}

	int pick = 0;
	for (int j = 1; j < n; j++) {
		if (connected[j] != connected[pick]) {
			if (connected[j])
				pick = j;
		} else if (hasConnectors[j] != hasConnectors[pick]) {
			if (hasConnectors[j])
				pick = j;
		} else if (bootVga[j] && !bootVga[pick])
			pick = j;
	}

	for (int j = 0; j < n; j++) {
		if (j != pick)
			libseat_close_device(seat, ids[j]);
	}

	deviceId = ids[pick];
	fd = fds[pick];
	cardIndex = indexes[pick];
	if (connected[pick]) {
		why = bootVga[pick] ? "connected connector, boot_vga"
			: "connected connector";
	} else if (hasConnectors[pick])
		why = "can drive a display, none connected";
	else
		why = "first device that opens";
	return true;
}


#endif // _DRM_DEVICE_SELECT_H_
