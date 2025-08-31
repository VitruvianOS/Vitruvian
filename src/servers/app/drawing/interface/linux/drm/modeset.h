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
void modeset_draw(void);
void modeset_cleanup(int fd);

#ifdef __cplusplus
}
#endif


#endif
