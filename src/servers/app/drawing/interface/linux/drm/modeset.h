/*
 * modeset - DRM Modesetting Example
 *
 * Copyright (?), David Rheinsberg <david.rheinsberg@gmail.com>
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the GPL License.
 */

#ifndef _MODESET_H
#define _MODESET_H

#include <xf86drm.h>
#include <xf86drmMode.h>

struct modeset_dev {
	struct modeset_dev *next;

	uint32_t width;
	uint32_t height;
	uint32_t stride;
	uint32_t size;
	uint32_t handle;
	uint8_t *map;

	/* back buffer for double buffering */
	uint32_t back_stride;
	uint32_t back_size;
	uint32_t back_handle;
	uint8_t *back_map;
	uint32_t back_fb;

	drmModeModeInfo mode;
	uint32_t fb;
	uint32_t conn;
	uint32_t crtc;
	drmModeCrtc *saved_crtc;
};

struct modeset_dev;

#pragma once
#ifdef __cplusplus
extern "C"
{
#endif

struct modeset_dev* get_dev();
int modeset_find_crtc(int fd, drmModeRes *res, drmModeConnector *conn,
			     struct modeset_dev *dev);
int modeset_create_fb(int fd, struct modeset_dev *dev);
int modeset_setup_dev(int fd, drmModeRes *res, drmModeConnector *conn,
			     struct modeset_dev *dev);
int modeset_open(int *out, const char *node);
int modeset_prepare(int fd);
int modeset_create_back_fb(int fd, struct modeset_dev *dev);
void modeset_draw(void);
void modeset_cleanup(int fd);

// Hotplug support
struct modeset_dev* modeset_dev_create(int fd, uint32_t connector_id);
void modeset_dev_destroy(int fd, struct modeset_dev* dev);
int  modeset_add_connector(int fd, uint32_t connector_id);
void modeset_remove_connector(uint32_t connector_id);

#ifdef __cplusplus
}
#endif


#endif
