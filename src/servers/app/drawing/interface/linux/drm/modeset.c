/*
 * modeset - DRM Modesetting Example
 *
 * Copyright (?), David Rheinsberg <david.rheinsberg@gmail.com>
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the GPL License.
 */

/*
 * DRM Modesetting Howto
 * This document describes the DRM modesetting API. Before we can use the DRM
 * API, we have to include xf86drm.h and xf86drmMode.h. Both are provided by
 * libdrm which every major distribution ships by default. It has no other
 * dependencies and is pretty small.
 *
 * Please ignore all forward-declarations of functions which are used later. I
 * reordered the functions so you can read this document from top to bottom. If
 * you reimplement it, you would probably reorder the functions to avoid all the
 * nasty forward declarations.
 *
 * For easier reading, we ignore all memory-allocation errors of malloc() and
 * friends here. However, we try to correctly handle all other kinds of errors
 * that may occur.
 *
 * All functions and global variables are prefixed with "modeset_*" in this
 * file. So it should be clear whether a function is a local helper or if it is
 * provided by some external library.
 */

#include "modeset.h"

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm/drm_fourcc.h>

#ifdef HAVE_GBM
#include <gbm.h>
#endif

/*struct modeset_dev;
static int modeset_find_crtc(int fd, drmModeRes *res, drmModeConnector *conn,
			     struct modeset_dev *dev);
static int modeset_create_fb(int fd, struct modeset_dev *dev);
static int modeset_setup_dev(int fd, drmModeRes *res, drmModeConnector *conn,
			     struct modeset_dev *dev);
static int modeset_open(int *out, const char *node);
static int modeset_prepare(int fd);
static void modeset_draw(void);
static void modeset_cleanup(int fd);
*/

struct modeset_dev *modeset_list = NULL;

struct modeset_dev* get_dev() {
	return modeset_list;
}

/*
 * When the linux kernel detects a graphics-card on your machine, it loads the
 * correct device driver (located in kernel-tree at ./drivers/gpu/drm/<xy>) and
 * provides two character-devices to control it. Udev (or whatever hotplugging
 * application you use) will create them as:
 *     /dev/dri/card0
 *     /dev/dri/controlID64
 * We only need the first one. You can hard-code this path into your application
 * like we do here, but it is recommended to use libudev with real hotplugging
 * and multi-seat support. However, this is beyond the scope of this document.
 * Also note that if you have multiple graphics-cards, there may also be
 * /dev/dri/card1, /dev/dri/card2, ...
 *
 * We simply use /dev/dri/card0 here but the user can specify another path on
 * the command line.
 *
 * modeset_open(out, node): This small helper function opens the DRM device
 * which is given as @node. The new fd is stored in @out on success. On failure,
 * a negative error code is returned.
 * After opening the file, we also check for the DRM_CAP_DUMB_BUFFER capability.
 * If the driver supports this capability, we can create simple memory-mapped
 * buffers without any driver-dependent code. As we want to avoid any radeon,
 * nvidia, intel, etc. specific code, we depend on DUMB_BUFFERs here.
 */


int modeset_open(int *out, const char *node)
{
	int fd, ret;
	uint64_t has_dumb;

	fd = open(node, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		ret = -errno;
		fprintf(stderr, "cannot open '%s': %m\n", node);
		return ret;
	}

	if (drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &has_dumb) < 0 ||
	    !has_dumb) {
		fprintf(stderr, "drm device '%s' does not support dumb buffers\n",
			node);
		close(fd);
		return -EOPNOTSUPP;
	}

	*out = fd;
	return 0;
}

/*
 * As a next step we need to find our available display devices. libdrm provides
 * a drmModeRes structure that contains all the needed information. We can
 * retrieve it via drmModeGetResources(fd) and free it via
 * drmModeFreeResources(res) again.
 *
 * A physical connector on your graphics card is called a "connector". You can
 * plug a monitor into it and control what is displayed. We are definitely
 * interested in what connectors are currently used, so we simply iterate
 * through the list of connectors and try to display a test-picture on each
 * available monitor.
 * However, this isn't as easy as it sounds. First, we need to check whether the
 * connector is actually used (a monitor is plugged in and turned on). Then we
 * need to find a CRTC that can control this connector. CRTCs are described
 * later on. After that we create a framebuffer object. If we have all this, we
 * can mmap() the framebuffer and draw a test-picture into it. Then we can tell
 * the DRM device to show the framebuffer on the given CRTC with the selected
 * connector.
 *
 * As we want to draw moving pictures on the framebuffer, we actually have to
 * remember all these settings. Therefore, we create one "struct modeset_dev"
 * object for each connector+crtc+framebuffer pair that we successfully
 * initialized and push it into the global device-list.
 *
 * Each field of this structure is described when it is first used. But as a
 * summary:
 * "struct modeset_dev" contains: {
 *  - @next: points to the next device in the single-linked list
 *
 *  - @width: width of our buffer object
 *  - @height: height of our buffer object
 *  - @stride: stride value of our buffer object
 *  - @size: size of the memory mapped buffer
 *  - @handle: a DRM handle to the buffer object that we can draw into
 *  - @map: pointer to the memory mapped buffer
 *
 *  - @mode: the display mode that we want to use
 *  - @fb: a framebuffer handle with our buffer object as scanout buffer
 *  - @conn: the connector ID that we want to use with this buffer
 *  - @crtc: the crtc ID that we want to use with this connector
 *  - @saved_crtc: the configuration of the crtc before we changed it. We use it
 *                 so we can restore the same mode when we exit.
 * }
 */

/*
 * So as next step we need to actually prepare all connectors that we find. We
 * do this in this little helper function:
 *
 * modeset_prepare(fd): This helper function takes the DRM fd as argument and
 * then simply retrieves the resource-info from the device. It then iterates
 * through all connectors and calls other helper functions to initialize this
 * connector (described later on).
 * If the initialization was successful, we simply add this object as new device
 * into the global modeset device list.
 *
 * The resource-structure contains a list of all connector-IDs. We use the
 * helper function drmModeGetConnector() to retrieve more information on each
 * connector. After we are done with it, we free it again with
 * drmModeFreeConnector().
 * Our helper modeset_setup_dev() returns -ENOENT if the connector is currently
 * unused and no monitor is plugged in. So we can ignore this connector.
 */


int modeset_prepare(int fd)
{
	drmModeRes *res;
	drmModeConnector *conn;
	int i;
	struct modeset_dev *dev;
	int ret;

	/* retrieve resources */
	res = drmModeGetResources(fd);
	if (!res) {
		fprintf(stderr, "cannot retrieve DRM resources (%d): %m\n",
			errno);
		return -errno;
	}

	/* iterate all connectors */
	for (i = 0; i < res->count_connectors; ++i) {
		/* get information for each connector */
		conn = drmModeGetConnector(fd, res->connectors[i]);
		if (!conn) {
			fprintf(stderr, "cannot retrieve DRM connector %u:%u (%d): %m\n",
				i, res->connectors[i], errno);
			continue;
		}

		/* create a device structure */
		dev = malloc(sizeof(*dev));
		memset(dev, 0, sizeof(*dev));
		dev->conn = conn->connector_id;

		/* call helper function to prepare this connector */
		ret = modeset_setup_dev(fd, res, conn, dev);
		if (ret) {
			if (ret != -ENOENT) {
				errno = -ret;
				fprintf(stderr, "cannot setup device for connector %u:%u (%d): %m\n",
					i, res->connectors[i], errno);
			}
			free(dev);
			drmModeFreeConnector(conn);
			continue;
		}

		/* free connector data and link device into global list */
		drmModeFreeConnector(conn);
		dev->next = modeset_list;
		modeset_list = dev;
	}

	/* free resources again */
	drmModeFreeResources(res);
	return 0;
}

/*
 * Now we dig deeper into setting up a single connector. As described earlier,
 * we need to check several things first:
 *   * If the connector is currently unused, that is, no monitor is plugged in,
 *     then we can ignore it.
 *   * We have to find a suitable resolution and refresh-rate. All this is
 *     available in drmModeModeInfo structures saved for each crtc. We simply
 *     use the first mode that is available. This is always the mode with the
 *     highest resolution.
 *     A more sophisticated mode-selection should be done in real applications,
 *     though.
 *   * Then we need to find an CRTC that can drive this connector. A CRTC is an
 *     internal resource of each graphics-card. The number of CRTCs controls how
 *     many connectors can be controlled indepedently. That is, a graphics-cards
 *     may have more connectors than CRTCs, which means, not all monitors can be
 *     controlled independently.
 *     There is actually the possibility to control multiple connectors via a
 *     single CRTC if the monitors should display the same content. However, we
 *     do not make use of this here.
 *     So think of connectors as pipelines to the connected monitors and the
 *     CRTCs are the controllers that manage which data goes to which pipeline.
 *     If there are more pipelines than CRTCs, then we cannot control all of
 *     them at the same time.
 *   * We need to create a framebuffer for this connector. A framebuffer is a
 *     memory buffer that we can write XRGB32 data into. So we use this to
 *     render our graphics and then the CRTC can scan-out this data from the
 *     framebuffer onto the monitor.
 */

int modeset_setup_dev(int fd, drmModeRes *res, drmModeConnector *conn,
			     struct modeset_dev *dev)
{
	int ret;

	/* check if a monitor is connected */
	if (conn->connection != DRM_MODE_CONNECTED) {
		fprintf(stderr, "ignoring unused connector %u\n",
			conn->connector_id);
		return -ENOENT;
	}

	/* check if there is at least one valid mode */
	if (conn->count_modes == 0) {
		fprintf(stderr, "no valid mode for connector %u\n",
			conn->connector_id);
		return -EFAULT;
	}

	for (int i = 0; i < conn->count_modes; i++) {
		fprintf(stderr, "mode %ux%u%s\n",
			conn->modes[i].hdisplay, conn->modes[i].vdisplay,
			(conn->modes[i].type & DRM_MODE_TYPE_PREFERRED)
				? " [preferred]" : "");
	}

	/* Prefer the EDID-flagged preferred mode; fall back to modes[0]
	 * (which is conventionally the highest-resolution mode).  Matters
	 * for TVs/projectors where modes[0] isn't always the right pick. */
	drmModeModeInfo* picked = NULL;
	for (int i = 0; i < conn->count_modes; i++) {
		if (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
			picked = &conn->modes[i];
			break;
		}
	}
	if (picked == NULL)
		picked = &conn->modes[0];

	memcpy(&dev->mode, picked, sizeof(dev->mode));
	dev->width  = picked->hdisplay;
	dev->height = picked->vdisplay;
	fprintf(stderr, "mode for connector %u is %ux%u\n",
		conn->connector_id, dev->width, dev->height);

	/* find a crtc for this connector */
	ret = modeset_find_crtc(fd, res, conn, dev);
	if (ret) {
		fprintf(stderr, "no valid crtc for connector %u\n",
			conn->connector_id);
		return ret;
	}

	/* create a framebuffer for this CRTC */
	ret = modeset_create_fb(fd, dev);
	if (ret) {
		fprintf(stderr, "cannot create framebuffer for connector %u\n",
			conn->connector_id);
		return ret;
	}

	return 0;
}

/*
 * modeset_find_crtc(fd, res, conn, dev): This small helper tries to find a
 * suitable CRTC for the given connector. We have actually have to introduce one
 * more DRM object to make this more clear: Encoders.
 * Encoders help the CRTC to convert data from a framebuffer into the right
 * format that can be used for the chosen connector. We do not have to
 * understand any more of these conversions to make use of it. However, you must
 * know that each connector has a limited list of encoders that it can use. And
 * each encoder can only work with a limited list of CRTCs. So what we do is
 * trying each encoder that is available and looking for a CRTC that this
 * encoder can work with. If we find the first working combination, we are happy
 * and write it into the @dev structure.
 * But before iterating all available encoders, we first try the currently
 * active encoder+crtc on a connector to avoid a full modeset.
 *
 * However, before we can use a CRTC we must make sure that no other device,
 * that we setup previously, is already using this CRTC. Remember, we can only
 * drive one connector per CRTC! So we simply iterate through the "modeset_list"
 * of previously setup devices and check that this CRTC wasn't used before.
 * Otherwise, we continue with the next CRTC/Encoder combination.
 */

int modeset_find_crtc(int fd, drmModeRes *res, drmModeConnector *conn,
			     struct modeset_dev *dev)
{
	drmModeEncoder *enc;
	int i, j;
	uint32_t crtc;
	struct modeset_dev *iter;

	/* first try the currently conected encoder+crtc */
	if (conn->encoder_id)
		enc = drmModeGetEncoder(fd, conn->encoder_id);
	else
		enc = NULL;

	if (enc) {
		if (enc->crtc_id) {
			crtc = enc->crtc_id;
			for (iter = modeset_list; iter; iter = iter->next) {
				if (iter->crtc == crtc) {
					crtc = 0;
					break;
				}
			}

			if (crtc != 0) {
				drmModeFreeEncoder(enc);
				dev->crtc = crtc;
				return 0;
			}
		}

		drmModeFreeEncoder(enc);
	}

	/* If the connector is not currently bound to an encoder or if the
	 * encoder+crtc is already used by another connector (actually unlikely
	 * but lets be safe), iterate all other available encoders to find a
	 * matching CRTC. */
	for (i = 0; i < conn->count_encoders; ++i) {
		enc = drmModeGetEncoder(fd, conn->encoders[i]);
		if (!enc) {
			fprintf(stderr, "cannot retrieve encoder %u:%u (%d): %m\n",
				i, conn->encoders[i], errno);
			continue;
		}

		/* iterate all global CRTCs */
		for (j = 0; j < res->count_crtcs; ++j) {
			/* check whether this CRTC works with the encoder */
			if (!(enc->possible_crtcs & (1 << j)))
				continue;

			/* check that no other device already uses this CRTC */
			crtc = res->crtcs[j];
			for (iter = modeset_list; iter; iter = iter->next) {
				if (iter->crtc == crtc) {
					crtc = 0;
					break;
				}
			}

			/* we have found a CRTC, so save it and return */
			if (crtc != 0) {
				drmModeFreeEncoder(enc);
				dev->crtc = crtc;
				return 0;
			}
		}

		drmModeFreeEncoder(enc);
	}

	fprintf(stderr, "cannot find suitable CRTC for connector %u\n",
		conn->connector_id);
	return -ENOENT;
}

/*
 * modeset_create_fb(fd, dev): After we have found a crtc+connector+mode
 * combination, we need to actually create a suitable framebuffer that we can
 * use with it. There are actually two ways to do that:
 *   * We can create a so called "dumb buffer". This is a buffer that we can
 *     memory-map via mmap() and every driver supports this. We can use it for
 *     unaccelerated software rendering on the CPU.
 *   * We can use libgbm to create buffers available for hardware-acceleration.
 *     libgbm is an abstraction layer that creates these buffers for each
 *     available DRM driver. As there is no generic API for this, each driver
 *     provides its own way to create these buffers.
 *     We can then use such buffers to create OpenGL contexts with the mesa3D
 *     library.
 * We use the first solution here as it is much simpler and doesn't require any
 * external libraries. However, if you want to use hardware-acceleration via
 * OpenGL, it is actually pretty easy to create such buffers with libgbm and
 * libEGL. But this is beyond the scope of this document.
 *
 * So what we do is requesting a new dumb-buffer from the driver. We specify the
 * same size as the current mode that we selected for the connector.
 * Then we request the driver to prepare this buffer for memory mapping. After
 * that we perform the actual mmap() call. So we can now access the framebuffer
 * memory directly via the dev->map memory map.
 */

int modeset_create_fb(int fd, struct modeset_dev *dev)
{
	struct drm_mode_create_dumb creq;
	struct drm_mode_map_dumb mreq;
	int ret;

	memset(&creq, 0, sizeof(creq));
	creq.width = dev->width;
	creq.height = dev->height;
	creq.bpp = 32;
	ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
	if (ret < 0) {
		fprintf(stderr, "cannot create dumb buffer (%d): %m\n",
			errno);
		return -errno;
	}
	dev->stride = creq.pitch;
	dev->size = creq.size;
	dev->handle = creq.handle;

	{
		uint32_t handles[4] = { dev->handle, 0, 0, 0 };
		uint32_t pitches[4] = { dev->stride, 0, 0, 0 };
		uint32_t offsets[4] = { 0, 0, 0, 0 };
		ret = drmModeAddFB2(fd, dev->width, dev->height, DRM_FORMAT_XRGB8888,
				    handles, pitches, offsets, &dev->fb, 0);
		if (ret) {
			fprintf(stderr, "cannot create framebuffer (drmModeAddFB2 failed, %d): %m\n",
				errno);
			ret = drmModeAddFB(fd, dev->width, dev->height, 24, 32, dev->stride,
					   dev->handle, &dev->fb);
			if (ret) {
				fprintf(stderr, "cannot create framebuffer (legacy fallback failed, %d): %m\n",
					errno);
				ret = -errno;
				struct drm_mode_destroy_dumb dreq = {};
				dreq.handle = dev->handle;
				drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
				dev->handle = 0;
				return ret;
			}
		}
	}

	memset(&mreq, 0, sizeof(mreq));
	mreq.handle = dev->handle;
	ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
	if (ret) {
		fprintf(stderr, "cannot map dumb buffer (%d): %m\n",
			errno);
		drmModeRmFB(fd, dev->fb);
		dev->fb = 0;
		struct drm_mode_destroy_dumb dreq = {};
		dreq.handle = dev->handle;
		drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
		dev->handle = 0;
		return -errno;
	}

	printf("offset %llu\n", (unsigned long long)mreq.offset);
	dev->map = mmap(0, dev->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		        fd, mreq.offset);
	if (dev->map == MAP_FAILED) {
		fprintf(stderr, "cannot mmap dumb buffer (%d): %m\n",
			errno);
		drmModeRmFB(fd, dev->fb);
		dev->fb = 0;
		struct drm_mode_destroy_dumb dreq = {};
		dreq.handle = dev->handle;
		drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
		dev->handle = 0;
		return -errno;
	}

	memset(dev->map, 0, dev->size);
	return 0;
}

int modeset_create_back_fb(int fd, struct modeset_dev *dev)
{
	struct drm_mode_create_dumb creq;
	struct drm_mode_map_dumb mreq;
	int ret;

	memset(&creq, 0, sizeof(creq));
	creq.width = dev->width;
	creq.height = dev->height;
	creq.bpp = 32;
	ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
	if (ret < 0) {
		fprintf(stderr, "back fb: cannot create dumb buffer (%d): %m\n", errno);
		return -errno;
	}
	dev->back_stride = creq.pitch;
	dev->back_size   = creq.size;
	dev->back_handle = creq.handle;

	{
		uint32_t handles[4] = { dev->back_handle, 0, 0, 0 };
		uint32_t pitches[4] = { dev->back_stride, 0, 0, 0 };
		uint32_t offsets[4] = { 0, 0, 0, 0 };
		ret = drmModeAddFB2(fd, dev->width, dev->height, DRM_FORMAT_XRGB8888,
				    handles, pitches, offsets, &dev->back_fb, 0);
		if (ret) {
			ret = drmModeAddFB(fd, dev->width, dev->height, 24, 32,
				dev->back_stride, dev->back_handle, &dev->back_fb);
			if (ret) {
				fprintf(stderr, "back fb: cannot create framebuffer (failed, %d): %m\n", errno);
				struct drm_mode_destroy_dumb dreq = {};
				dreq.handle = dev->back_handle;
				drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
				dev->back_handle = 0;
				return -errno;
			}
		}
	}

	memset(&mreq, 0, sizeof(mreq));
	mreq.handle = dev->back_handle;
	ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
	if (ret) {
		fprintf(stderr, "back fb: cannot map dumb buffer (%d): %m\n", errno);
		drmModeRmFB(fd, dev->back_fb);
		dev->back_fb = 0;
		struct drm_mode_destroy_dumb dreq = {};
		dreq.handle = dev->back_handle;
		drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
		dev->back_handle = 0;
		return -errno;
	}

	dev->back_map = mmap(0, dev->back_size, PROT_READ | PROT_WRITE,
		MAP_SHARED, fd, mreq.offset);
	if (dev->back_map == MAP_FAILED) {
		fprintf(stderr, "back fb: cannot mmap dumb buffer (%d): %m\n", errno);
		dev->back_map = NULL;
		drmModeRmFB(fd, dev->back_fb);
		dev->back_fb = 0;
		struct drm_mode_destroy_dumb dreq = {};
		dreq.handle = dev->back_handle;
		drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
		dev->back_handle = 0;
		return -errno;
	}

	memset(dev->back_map, 0, dev->back_size);
	return 0;
}



/*
 * Finally! We have a connector with a suitable CRTC. We know which mode we want
 * to use and we have a framebuffer of the correct size that we can write to.
 * There is nothing special left to do. We only have to program the CRTC to
 * connect each new framebuffer to each selected connector for each combination
 * that we saved in the global modeset_list.
 * This is done with a call to drmModeSetCrtc().
 *
 * So we are ready for our main() function. First we check whether the user
 * specified a DRM device on the command line, otherwise we use the default
 * /dev/dri/card0. Then we open the device via modeset_open(). modeset_prepare()
 * prepares all connectors and we can loop over "modeset_list" and call
 * drmModeSetCrtc() on every CRTC/connector combination.
 *
 * But printing empty black pages is boring so we have another helper function
 * modeset_draw() that draws some colors into the framebuffer for 5 seconds and
 * then returns. And then we have all the cleanup functions which correctly free
 * all devices again after we used them. All these functions are described below
 * the main() function.
 *
 * As a side note: drmModeSetCrtc() actually takes a list of connectors that we
 * want to control with this CRTC. We pass only one connector, though. As
 * explained earlier, if we used multiple connectors, then all connectors would
 * have the same controlling framebuffer so the output would be cloned. This is
 * most often not what you want so we avoid explaining this feature here.
 * Furthermore, all connectors will have to run with the same mode, which is
 * also often not guaranteed. So instead, we only use one connector per CRTC.
 *
 * Before calling drmModeSetCrtc() we also save the current CRTC configuration.
 * This is used in modeset_cleanup() to restore the CRTC to the same mode as was
 * before we changed it.
 * If we don't do this, the screen will stay blank after we exit until another
 * application performs modesetting itself.
 */

#if 0
int main(int argc, char **argv)
{
	int ret, fd;
	const char *card;
	struct modeset_dev *iter;

	/* check which DRM device to open */
	if (argc > 1)
		card = argv[1];
	else
		card = "/dev/dri/card0";

	fprintf(stderr, "using card '%s'\n", card);

	/* open the DRM device */
	ret = modeset_open(&fd, card);
	if (ret)
		goto out_return;

	/* prepare all connectors and CRTCs */
	ret = modeset_prepare(fd);
	if (ret)
		goto out_close;

	/* perform actual modesetting on each found connector+CRTC */
	for (iter = modeset_list; iter; iter = iter->next) {
		iter->saved_crtc = drmModeGetCrtc(fd, iter->crtc);
		ret = drmModeSetCrtc(fd, iter->crtc, iter->fb, 0, 0,
				     &iter->conn, 1, &iter->mode);
		if (ret)
			fprintf(stderr, "cannot set CRTC for connector %u (%d): %m\n",
				iter->conn, errno);
	}

	/* draw some colors for 5seconds */
	modeset_draw();

	/* cleanup everything */
	modeset_cleanup(fd);

	ret = 0;

out_close:
	close(fd);
out_return:
	if (ret) {
		errno = -ret;
		fprintf(stderr, "modeset failed with error %d: %m\n", errno);
	} else {
		fprintf(stderr, "exiting\n");
	}
	return ret;
}
#endif
/*
 * A short helper function to compute a changing color value. No need to
 * understand it.
 */

uint8_t next_color(bool *up, uint8_t cur, unsigned int mod)
{
	uint8_t next;

	next = cur + (*up ? 1 : -1) * (rand() % mod);
	if ((*up && next < cur) || (!*up && next > cur)) {
		*up = !*up;
		next = cur;
	}

	return next;
}

/*
 * modeset_draw(): This draws a solid color into all configured framebuffers.
 * Every 100ms the color changes to a slightly different color so we get some
 * kind of smoothly changing color-gradient.
 *
 * The color calculation can be ignored as it is pretty boring. So the
 * interesting stuff is iterating over "modeset_list" and then through all lines
 * and width. We then set each pixel individually to the current color.
 *
 * We do this 50 times as we sleep 100ms after each redraw round. This makes
 * 50*100ms = 5000ms = 5s so it takes about 5seconds to finish this loop.
 *
 * Please note that we draw directly into the framebuffer. This means that you
 * will see flickering as the monitor might refresh while we redraw the screen.
 * To avoid this you would need to use two framebuffers and a call to
 * drmModeSetCrtc() to switch between both buffers.
 * You can also use drmModePageFlip() to do a vsync'ed pageflip. But this is
 * beyond the scope of this document.
 */

void modeset_draw(void)
{
	uint8_t r, g, b;
	bool r_up, g_up, b_up;
	unsigned int i, j, k, off;
	struct modeset_dev *iter;

	srand(time(NULL));
	r = rand() % 0xff;
	g = rand() % 0xff;
	b = rand() % 0xff;
	r_up = g_up = b_up = true;

	for (i = 0; i < 50; ++i) {
		r = next_color(&r_up, r, 20);
		g = next_color(&g_up, g, 10);
		b = next_color(&b_up, b, 5);

		for (iter = modeset_list; iter; iter = iter->next) {
			for (j = 0; j < iter->height; ++j) {
				for (k = 0; k < iter->width; ++k) {
					off = iter->stride * j + k * 4;
					*(uint32_t*)&iter->map[off] =
						     (r << 16) | (g << 8) | b;
				}
			}
		}
	}
}

/*
 * modeset_cleanup(fd): This cleans up all the devices we created during
 * modeset_prepare(). It resets the CRTCs to their saved states and deallocates
 * all memory.
 * It should be pretty obvious how all of this works.
 */

struct modeset_dev*
modeset_dev_create(int fd, uint32_t connector_id)
{
	drmModeRes* res = drmModeGetResources(fd);
	if (!res)
		return NULL;

	drmModeConnector* conn = drmModeGetConnector(fd, connector_id);
	if (!conn) {
		drmModeFreeResources(res);
		return NULL;
	}

	struct modeset_dev* dev = malloc(sizeof(*dev));
	if (!dev) {
		drmModeFreeConnector(conn);
		drmModeFreeResources(res);
		return NULL;
	}
	memset(dev, 0, sizeof(*dev));
	dev->conn = connector_id;

	int ret = modeset_setup_dev(fd, res, conn, dev);
	drmModeFreeConnector(conn);
	drmModeFreeResources(res);

	if (ret != 0) {
		free(dev);
		return NULL;
	}
	return dev;
}


void
modeset_dev_destroy(int fd, struct modeset_dev* dev)
{
	if (!dev)
		return;

	if (dev->saved_crtc) {
		drmModeSetCrtc(fd, dev->saved_crtc->crtc_id,
			dev->saved_crtc->buffer_id,
			dev->saved_crtc->x, dev->saved_crtc->y,
			&dev->conn, 1, &dev->saved_crtc->mode);
		drmModeFreeCrtc(dev->saved_crtc);
	}

	if (dev->map)
		munmap(dev->map, dev->size);

	if (dev->fb)
		drmModeRmFB(fd, dev->fb);

	if (dev->handle) {
		struct drm_mode_destroy_dumb dreq;
		memset(&dreq, 0, sizeof(dreq));
		dreq.handle = dev->handle;
		drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
	}

	if (dev->back_map)
		munmap(dev->back_map, dev->back_size);

	if (dev->back_fb)
		drmModeRmFB(fd, dev->back_fb);

	if (dev->back_handle) {
		struct drm_mode_destroy_dumb dreq;
		memset(&dreq, 0, sizeof(dreq));
		dreq.handle = dev->back_handle;
		drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
	}

	if (dev->cursor_fb) {
		drmModeRmFB(fd, dev->cursor_fb);
		dev->cursor_fb = 0;
	}
	if (dev->cursor_map) {
		munmap(dev->cursor_map, dev->cursor_size);
		dev->cursor_map = NULL;
	}
	if (dev->cursor_handle) {
		struct drm_mode_destroy_dumb dreq;
		memset(&dreq, 0, sizeof(dreq));
		dreq.handle = dev->cursor_handle;
		drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
		dev->cursor_handle = 0;
	}

	free(dev);
}


int
modeset_add_connector(int fd, uint32_t connector_id)
{
	/* Check not already in list */
	struct modeset_dev* iter;
	for (iter = modeset_list; iter; iter = iter->next) {
		if (iter->conn == connector_id)
			return 0;
	}

	struct modeset_dev* dev = modeset_dev_create(fd, connector_id);
	if (!dev)
		return -ENOENT;

	dev->saved_crtc = drmModeGetCrtc(fd, dev->crtc);
	drmModeSetCrtc(fd, dev->crtc, dev->fb, 0, 0, &dev->conn, 1, &dev->mode);

	dev->next = modeset_list;
	modeset_list = dev;

	fprintf(stderr, "hotplug: added connector %u (%ux%u)\n",
		connector_id, dev->width, dev->height);
	return 0;
}


void
modeset_remove_connector(int fd, uint32_t connector_id)
{
	struct modeset_dev** pp = &modeset_list;
	while (*pp) {
		if ((*pp)->conn == connector_id) {
			struct modeset_dev* dev = *pp;
			*pp = dev->next;
			fprintf(stderr, "hotplug: removed connector %u\n", connector_id);
			/* full teardown — unmaps buffers, drmModeRmFB, destroys
			 * dumb BOs, frees the struct.  Replaces the previous
			 * bare free(dev) which leaked the framebuffer + GEM
			 * handles on every hotplug-out. */
			modeset_dev_destroy(fd, dev);
			return;
		}
		pp = &(*pp)->next;
	}
}


void modeset_cleanup(int fd)
{
	struct modeset_dev *iter;
	struct drm_mode_destroy_dumb dreq;

	while (modeset_list) {
		/* remove from global list */
		iter = modeset_list;
		modeset_list = iter->next;

		/* restore saved CRTC configuration (only if we ever saved it
		 * — early-init failures can leave saved_crtc NULL) */
		if (iter->saved_crtc) {
			drmModeSetCrtc(fd,
				       iter->saved_crtc->crtc_id,
				       iter->saved_crtc->buffer_id,
				       iter->saved_crtc->x,
				       iter->saved_crtc->y,
				       &iter->conn,
				       1,
				       &iter->saved_crtc->mode);
			drmModeFreeCrtc(iter->saved_crtc);
			iter->saved_crtc = NULL;
		}

		/* unmap buffer (guard NULL — same early-init concern) */
		if (iter->map)
			munmap(iter->map, iter->size);

		/* delete framebuffer */
		drmModeRmFB(fd, iter->fb);
		if (iter->back_fb) {
			drmModeRmFB(fd, iter->back_fb);
			iter->back_fb = 0;
		}
		/* delete dumb buffer */
		memset(&dreq, 0, sizeof(dreq));
		dreq.handle = iter->handle;
		drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);

		if (iter->back_handle) {
			struct drm_mode_destroy_dumb bdreq = {};
			bdreq.handle = iter->back_handle;
			drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &bdreq);
		}

		if (iter->cursor_fb) {
			drmModeRmFB(fd, iter->cursor_fb);
			iter->cursor_fb = 0;
		}
		if (iter->cursor_map)
			munmap(iter->cursor_map, iter->cursor_size);
		if (iter->cursor_handle) {
			memset(&dreq, 0, sizeof(dreq));
			dreq.handle = iter->cursor_handle;
			drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
		}

		/* free allocated memory */
		free(iter);
	}
}


#ifdef HAVE_GBM
int
modeset_create_gbm_fb(int fd, struct gbm_device* gbm,
                      struct modeset_dev* dev, bool isBack)
{
	struct gbm_bo* bo = gbm_bo_create(gbm,
		dev->width, dev->height,
		GBM_FORMAT_XRGB8888,
		GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);

	if (!bo) {
		fprintf(stderr, "gbm_bo_create failed\n");
		return -ENOMEM;
	}

	uint32_t handle = gbm_bo_get_handle(bo).u32;
	uint32_t stride = gbm_bo_get_stride(bo);
	uint64_t modifier = gbm_bo_get_modifier(bo);
	uint32_t fb_id = 0;

	int ret;
	if (modifier != DRM_FORMAT_MOD_LINEAR && modifier != DRM_FORMAT_MOD_INVALID) {
		uint32_t handles[4] = { handle };
		uint32_t pitches[4] = { stride };
		uint32_t offsets[4] = { 0 };
		uint64_t modifiers[4] = { modifier };
		ret = drmModeAddFB2WithModifiers(fd, dev->width, dev->height,
			DRM_FORMAT_XRGB8888, handles, pitches, offsets, modifiers,
			&fb_id, DRM_MODE_FB_MODIFIERS);
	} else {
		uint32_t handles[4] = { handle };
		uint32_t pitches[4] = { stride };
		uint32_t offsets[4] = { 0 };
		ret = drmModeAddFB2(fd, dev->width, dev->height,
			DRM_FORMAT_XRGB8888, handles, pitches, offsets, &fb_id, 0);
	}

	if (ret) {
		fprintf(stderr, "drmModeAddFB2 for GBM bo failed: %m\n");
		gbm_bo_destroy(bo);
		return -errno;
	}

	uint32_t map_stride = 0;
	void* map_data = NULL;
	void* map = gbm_bo_map(bo, 0, 0, dev->width, dev->height,
		GBM_BO_TRANSFER_WRITE, &map_stride, &map_data);

	if (map == MAP_FAILED || map == NULL) {
		fprintf(stderr, "gbm_bo_map failed -- falling back to dumb buffer\n");
		drmModeRmFB(fd, fb_id);
		gbm_bo_destroy(bo);
		return -ENOTSUP;
	}

	if (isBack) {
		dev->back_bo       = bo;
		dev->back_fb       = fb_id;
		dev->back_stride   = map_stride;
		dev->back_map      = map;
		dev->back_size     = map_stride * dev->height;
		dev->back_map_data = map_data;
	} else {
		dev->front_bo       = bo;
		dev->fb             = fb_id;
		dev->stride         = map_stride;
		dev->map            = map;
		dev->size           = map_stride * dev->height;
		dev->front_map_data = map_data;
	}

	return 0;
}


int
modeset_create_render_fb(int fd, struct gbm_device* gbm,
                       struct modeset_dev* dev)
{
	struct gbm_bo* bo = gbm_bo_create(gbm,
		dev->width, dev->height,
		GBM_FORMAT_XRGB8888,
		GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);

	if (!bo) {
		fprintf(stderr, "gbm_bo_create failed for render buffer\n");
		return -ENOMEM;
	}

	uint32_t handle = gbm_bo_get_handle(bo).u32;
	uint32_t stride = gbm_bo_get_stride(bo);
	uint64_t modifier = gbm_bo_get_modifier(bo);
	uint32_t fb_id = 0;

	int ret;
	if (modifier != DRM_FORMAT_MOD_LINEAR && modifier != DRM_FORMAT_MOD_INVALID) {
		uint32_t handles[4] = { handle };
		uint32_t pitches[4] = { stride };
		uint32_t offsets[4] = { 0 };
		uint64_t modifiers[4] = { modifier };
		ret = drmModeAddFB2WithModifiers(fd, dev->width, dev->height,
			DRM_FORMAT_XRGB8888, handles, pitches, offsets, modifiers,
			&fb_id, DRM_MODE_FB_MODIFIERS);
	} else {
		uint32_t handles[4] = { handle };
		uint32_t pitches[4] = { stride };
		uint32_t offsets[4] = { 0 };
		ret = drmModeAddFB2(fd, dev->width, dev->height,
			DRM_FORMAT_XRGB8888, handles, pitches, offsets, &fb_id, 0);
	}

	if (ret) {
		fprintf(stderr, "drmModeAddFB2 for render bo failed: %m\n");
		gbm_bo_destroy(bo);
		return -errno;
	}

	uint32_t map_stride = 0;
	void* map_data = NULL;
	void* map = gbm_bo_map(bo, 0, 0, dev->width, dev->height,
		GBM_BO_TRANSFER_WRITE, &map_stride, &map_data);

	if (map == MAP_FAILED || map == NULL) {
		fprintf(stderr, "gbm_bo_map failed for render buffer\n");
		drmModeRmFB(fd, fb_id);
		gbm_bo_destroy(bo);
		return -ENOTSUP;
	}

	dev->render_bo       = bo;
	dev->render_fb       = fb_id;
	dev->render_stride   = map_stride;
	dev->render_map      = map;
	dev->render_map_data = map_data;

	return 0;
}
#endif


int
modeset_create_cursor_fb(int fd, struct modeset_dev *dev)
{
	struct drm_mode_create_dumb creq = {};
	creq.width  = 64;
	creq.height = 64;
	creq.bpp    = 32;
	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
		fprintf(stderr, "cursor: cannot create dumb buffer: %m\n");
		return -errno;
	}

	struct drm_mode_map_dumb mreq = { .handle = creq.handle };
	if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
		fprintf(stderr, "cursor: cannot map dumb buffer: %m\n");
		return -errno;
	}

	void* map = mmap(NULL, creq.size, PROT_READ | PROT_WRITE,
			 MAP_SHARED, fd, mreq.offset);
	if (map == MAP_FAILED) {
		fprintf(stderr, "cursor: mmap failed: %m\n");
		return -errno;
	}

	memset(map, 0, creq.size);
	dev->cursor_handle = creq.handle;
	dev->cursor_w      = 64;
	dev->cursor_h      = 64;
	dev->cursor_map    = (uint8_t*)map;
	dev->cursor_size   = creq.size;
	dev->cursor_ok     = true;

	/*
	 * Register the cursor BO as a real DRM framebuffer.  Atomic cursor
	 * plane commits require an fb_id (drmModeAddFB-derived), not a GEM
	 * handle.  Legacy drmModeSetCursor/SetCursor2 keep taking the GEM
	 * handle (API contract).  See app-server-fixup.md §3a.
	 */
	uint32_t handles[4] = { creq.handle, 0, 0, 0 };
	uint32_t pitches[4] = { 64 * 4,      0, 0, 0 };
	uint32_t offsets[4] = { 0, 0, 0, 0 };
	dev->cursor_fb = 0;
	if (drmModeAddFB2(fd, 64, 64, DRM_FORMAT_ARGB8888,
			handles, pitches, offsets, &dev->cursor_fb, 0) != 0) {
		fprintf(stderr,
			"cursor: drmModeAddFB2 failed (%d): %m -- atomic cursor "
			"plane will be unavailable; legacy cursor still works\n",
			errno);
		dev->cursor_fb = 0;
	}
	return 0;
}

/*
 * I hope this was a short but easy overview of the DRM modesetting API. The DRM
 * API offers much more capabilities including:
 *  - double-buffering or tripple-buffering (or whatever you want)
 *  - vsync'ed page-flips
 *  - hardware-accelerated rendering (for example via OpenGL)
 *  - output cloning
 *  - graphics-clients plus authentication
 *  - DRM planes/overlays/sprites
 *  - ...
 * If you are interested in these topics, I can currently only redirect you to
 * existing implementations, including:
 *  - plymouth (which uses dumb-buffers like this example; very easy to understand)
 *  - kmscon (which uses libuterm to do this)
 *  - wayland (very sophisticated DRM renderer; hard to understand fully as it
 *             uses more complicated techniques like DRM planes)
 *  - xserver (very hard to understand as it is split across many files/projects)
 *
 * But understanding how modesetting (as described in this document) works, is
 * essential to understand all further DRM topics.
 *
 * Any feedback is welcome. Feel free to use this code freely for your own
 * documentation or projects.
 *
 *  - Hosted on http://github.com/dvdhrm/docs
 *  - Written by David Rheinsberg <david.rheinsberg@gmail.com>
 */
