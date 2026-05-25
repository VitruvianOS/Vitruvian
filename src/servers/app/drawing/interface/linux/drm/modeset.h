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

#ifdef HAVE_GBM
#include <gbm.h>
#endif

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

#ifdef HAVE_GBM
	struct gbm_bo*  front_bo;
	struct gbm_bo*  back_bo;
	void*           front_map_data;
	void*           back_map_data;
	struct gbm_bo*  render_bo;
	void*           render_map_data;
	uint8_t*        render_map;
	uint32_t        render_stride;
	uint32_t        render_fb;
#endif

	uint32_t cursor_handle;
	uint32_t cursor_fb;
	uint32_t cursor_w;
	uint32_t cursor_h;
	uint8_t*  cursor_map;
	uint32_t cursor_size;
	bool      cursor_ok;
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

int modeset_create_render_fb(int fd, struct gbm_device* gbm,
                          struct modeset_dev* dev);

int modeset_create_cursor_fb(int fd, struct modeset_dev *dev);

#ifdef HAVE_GBM
int modeset_create_gbm_fb(int fd, struct gbm_device* gbm,
                          struct modeset_dev* dev, bool isBack);
#endif

// Hotplug support
struct modeset_dev* modeset_dev_create(int fd, uint32_t connector_id);
void modeset_dev_destroy(int fd, struct modeset_dev* dev);

void modeset_cleanup(int fd);
int  modeset_add_connector(int fd, uint32_t connector_id);
void modeset_remove_connector(int fd, uint32_t connector_id);

#ifdef __cplusplus
}
#endif


#endif
