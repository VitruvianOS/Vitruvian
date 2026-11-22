/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "vos_platform.h"

#include "compositor_adapter.h"
#include "DmabufPresenter.h"
#include "logging.h"

#include <GlDeviceSelect.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <cstring>
#include <vector>


// 30 Hz matches the FDO frame clock: it leaves llvmpipe CPU for video
// decoding. The tick completes WebKit's buffer pipeline (FrameDone).
static const guint kVosFrameTickMs = 33;


// Polls the swap chain for buffers app_server is done with; it observes
// the release, it does not decide it.
static const guint kReleasePollMs = 4;

const uint32_t kFormatXRGB8888 = 0x34325258;


struct _VosDisplay {
	WPEDisplay			parent;
	VosPlatformHost*	host;
	// Set when frames can go to the swap chain as dma-bufs.
	WPEDRMDevice*		drmDevice;
	WPEBufferFormats*	formats;
	// Withdrawn formats; WebKit may still hold the pointer.
	WPEBufferFormats*	retiredFormats;
};


struct _VosToplevel {
	WPEToplevel			parent;
	VosPlatformHost*	host;
};


struct _VosView {
	WPEView				parent;
	VosPlatformHost*	host;
	WPEBuffer*			pendingBuffer;
	guint				paceSource;
	guint				pollSource;
};


G_DEFINE_TYPE(VosDisplay, vos_display, WPE_TYPE_DISPLAY)
G_DEFINE_TYPE(VosToplevel, vos_toplevel, WPE_TYPE_TOPLEVEL)
G_DEFINE_TYPE(VosView, vos_view, WPE_TYPE_VIEW)


//	#pragma mark - VosView


static gboolean
_vos_view_pace_tick(gpointer data)
{
	auto* view = VOS_VIEW(data);
	view->paceSource = 0;
	if (view->pendingBuffer != nullptr) {
		// On screen: WebKit may produce the next frame, and this
		// buffer is free — the pixels were copied out already.
		wpe_view_buffer_rendered(WPE_VIEW(view), view->pendingBuffer);
		wpe_view_buffer_released(WPE_VIEW(view), view->pendingBuffer);
		g_object_unref(view->pendingBuffer);
		view->pendingBuffer = nullptr;
	}
	return G_SOURCE_REMOVE;
}


static gboolean
_vos_view_poll_tick(gpointer data)
{
	auto* view = VOS_VIEW(data);
	DmabufPresenter* presenter = view->host != nullptr
		&& view->host->compositor != nullptr
		? view->host->compositor->Presenter() : nullptr;
	if (presenter != nullptr && presenter->Poll())
		return G_SOURCE_CONTINUE;

	view->pollSource = 0;
	return G_SOURCE_REMOVE;
}


// Holds the buffer until the next tick: the renderer waits for
// FrameDone, so the tick paces the copying path.
static void
_vos_view_hold_for_tick(VosView* view, WPEBuffer* buffer)
{
	if (view->pendingBuffer != nullptr)
		g_object_unref(view->pendingBuffer);
	view->pendingBuffer = static_cast<WPEBuffer*>(g_object_ref(buffer));
	if (view->paceSource == 0) {
		view->paceSource = g_timeout_add(kVosFrameTickMs,
			_vos_view_pace_tick, view);
	}
}


static gboolean
_vos_view_render_shm(VosView* view, WPEBuffer* buffer, GError** error)
{
	WPEBufferSHM* shm = WPE_BUFFER_SHM(buffer);
	GBytes* bytes = wpe_buffer_shm_get_data(shm);
	if (bytes == nullptr)
		return FALSE;

	gsize size = 0;
	const uint8_t* data = static_cast<const uint8_t*>(g_bytes_get_data(bytes,
		&size));
	int width = wpe_buffer_get_width(buffer);
	int height = wpe_buffer_get_height(buffer);
	int stride = static_cast<int>(wpe_buffer_shm_get_stride(shm));

	if (view->host != nullptr && view->host->compositor != nullptr)
		view->host->compositor->PresentSHMFrame(data, stride, width, height);

	_vos_view_hold_for_tick(view, buffer);
	return TRUE;
}


// Linear dma-buf the swap chain could not take: read it like SHM.
static bool
_vos_view_copy_dmabuf(VosView* view, WPEBufferDMABuf* dmabuf, int width,
	int height)
{
	if (wpe_buffer_dma_buf_get_modifier(dmabuf) != 0
			|| view->host == nullptr || view->host->compositor == nullptr)
		return false;

	int fd = wpe_buffer_dma_buf_get_fd(dmabuf, 0);
	size_t offset = wpe_buffer_dma_buf_get_offset(dmabuf, 0);
	size_t stride = wpe_buffer_dma_buf_get_stride(dmabuf, 0);
	size_t length = offset + stride * (size_t)height;
	void* map = mmap(nullptr, length, PROT_READ, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED)
		return false;

	struct dma_buf_sync sync = { DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ };
	ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);
	view->host->compositor->PresentSHMFrame(
		static_cast<const uint8_t*>(map) + offset, (int)stride, width,
		height);
	sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ;
	ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);
	munmap(map, length);
	return true;
}


// After the swap chain gave up: advertise SHM only so WebKit stops
// choosing dma-bufs for new buffers.
static void
_vos_display_withdraw_dmabuf(WPEDisplay* wpeDisplay)
{
	auto* display = VOS_DISPLAY(wpeDisplay);
	if (display->formats == nullptr)
		return;
	if (display->retiredFormats == nullptr)
		display->retiredFormats = display->formats;
	else
		g_object_unref(display->formats);
	display->formats = nullptr;
	display->retiredFormats = nullptr;
}


static gboolean
_vos_view_render_dmabuf(VosView* view, WPEBuffer* buffer,
	const WPERectangle* damage, guint damageCount)
{
	WPEBufferDMABuf* dmabuf = WPE_BUFFER_DMA_BUF(buffer);
	int width = wpe_buffer_get_width(buffer);
	int height = wpe_buffer_get_height(buffer);
	CompositorAdapter* compositor = view->host != nullptr
		? view->host->compositor : nullptr;
	DmabufPresenter* presenter = compositor != nullptr
		? compositor->Presenter() : nullptr;

	// Zero copy: the chain imports one plane in XRGB8888.
	if (presenter != nullptr && presenter->IsReady()
			&& wpe_buffer_dma_buf_get_n_planes(dmabuf) == 1
			&& wpe_buffer_dma_buf_get_format(dmabuf) == kFormatXRGB8888) {
		BPrivate::swap_chain_external description;
		description.width = (uint32)width;
		description.height = (uint32)height;
		description.fourcc = BPrivate::kSwapFormatXRGB8888;
		description.stride = wpe_buffer_dma_buf_get_stride(dmabuf, 0);
		description.offset = wpe_buffer_dma_buf_get_offset(dmabuf, 0);
		description.modifier = wpe_buffer_dma_buf_get_modifier(dmabuf);

		switch (presenter->Present(buffer,
				wpe_buffer_dma_buf_get_fd(dmabuf, 0), description, damage,
				damageCount)) {
			case DmabufPresenter::kShown:
				compositor->SetDmabufActive(true);
				if (view->pollSource == 0) {
					view->pollSource = g_timeout_add(kReleasePollMs,
						_vos_view_poll_tick, view);
				}
				return TRUE;

			case DmabufPresenter::kBusy:
				// Still on screen from an earlier frame: that one gives
				// the buffer back.
				wpe_view_buffer_rendered(WPE_VIEW(view), buffer);
				return TRUE;

			case DmabufPresenter::kUnsupported:
				if (presenter->Failed()) {
					_vos_display_withdraw_dmabuf(
						wpe_view_get_display(WPE_VIEW(view)));
				}
				break;
		}
	}

	if (_vos_view_copy_dmabuf(view, dmabuf, width, height)) {
		_vos_view_hold_for_tick(view, buffer);
		return TRUE;
	}

	// A tiled buffer nobody can show or read: drop the frame so WebKit
	// keeps going at the next one.
	static bool sWarned = false;
	if (!sWarned) {
		sWarned = true;
		log_error("VosView: dma-buf refused (modifier %#llx), frames dropped",
			(unsigned long long)wpe_buffer_dma_buf_get_modifier(dmabuf));
	}
	wpe_view_buffer_rendered(WPE_VIEW(view), buffer);
	wpe_view_buffer_released(WPE_VIEW(view), buffer);
	return TRUE;
}


static gboolean
vos_view_render_buffer(WPEView* wpeView, WPEBuffer* buffer,
	const WPERectangle* damage, guint damageCount, GError** error)
{
	auto* view = VOS_VIEW(wpeView);
	if (WPE_IS_BUFFER_DMA_BUF(buffer))
		return _vos_view_render_dmabuf(view, buffer, damage, damageCount);

	if (!WPE_IS_BUFFER_SHM(buffer)) {
		g_set_error_literal(error, WPE_VIEW_ERROR,
			WPE_VIEW_ERROR_RENDER_FAILED,
			"VosView: unsupported buffer type");
		return FALSE;
	}
	return _vos_view_render_shm(view, buffer, error);
}


static gboolean
vos_view_can_be_mapped(WPEView*)
{
	return TRUE;
}


static void
vos_view_set_cursor_from_name(WPEView* wpeView, const char* name)
{
	auto* view = VOS_VIEW(wpeView);
	if (view->host != nullptr && view->host->onCursorName)
		view->host->onCursorName(name != nullptr ? name : "");
}


static void
vos_view_dispose(GObject* object)
{
	auto* view = VOS_VIEW(object);
	if (view->paceSource != 0) {
		g_source_remove(view->paceSource);
		view->paceSource = 0;
	}
	if (view->pollSource != 0) {
		g_source_remove(view->pollSource);
		view->pollSource = 0;
	}
	if (view->pendingBuffer != nullptr) {
		g_object_unref(view->pendingBuffer);
		view->pendingBuffer = nullptr;
	}
	if (view->host != nullptr && view->host->compositor != nullptr
			&& view->host->compositor->Presenter() != nullptr) {
		// The callbacks name this view: drop them before it dies.
		DmabufPresenter* presenter = view->host->compositor->Presenter();
		presenter->SetCallbacks(nullptr, nullptr);
		presenter->Flush();
	}

	G_OBJECT_CLASS(vos_view_parent_class)->dispose(object);
}


static void
vos_view_constructed(GObject* object)
{
	G_OBJECT_CLASS(vos_view_parent_class)->constructed(object);

	// A fresh view is 0x0 and unmapped; attach to the toplevel the
	// display created for it and take its size, like headless does.
	auto* wpeView = WPE_VIEW(object);
	g_signal_connect(wpeView, "notify::toplevel", G_CALLBACK(+[](WPEView* wpeView,
		GParamSpec*, gpointer) {
		auto* toplevel = wpe_view_get_toplevel(wpeView);
		if (toplevel == nullptr) {
			wpe_view_unmap(wpeView);
			return;
		}

		int width = 0;
		int height = 0;
		wpe_toplevel_get_size(toplevel, &width, &height);
		if (width > 0 && height > 0)
			wpe_view_resized(wpeView, width, height);
		wpe_view_map(wpeView);
	}), nullptr);
}


static void
vos_view_class_init(VosViewClass* viewClass)
{
	GObjectClass* objectClass = G_OBJECT_CLASS(viewClass);
	objectClass->constructed = vos_view_constructed;
	objectClass->dispose = vos_view_dispose;

	WPEViewClass* wpeViewClass = WPE_VIEW_CLASS(viewClass);
	wpeViewClass->render_buffer = vos_view_render_buffer;
	wpeViewClass->can_be_mapped = vos_view_can_be_mapped;
	wpeViewClass->set_cursor_from_name = vos_view_set_cursor_from_name;
}


static void
vos_view_init(VosView* view)
{
	view->host = nullptr;
	view->pendingBuffer = nullptr;
	view->paceSource = 0;
	view->pollSource = 0;
}


//	#pragma mark - VosToplevel


static gboolean
vos_toplevel_resize(WPEToplevel* toplevel, int width, int height)
{
	auto* self = VOS_TOPLEVEL(toplevel);
	if (self->host != nullptr && self->host->onResizeRequest)
		self->host->onResizeRequest(width, height);
	return TRUE;
}


static gboolean
vos_toplevel_set_fullscreen(WPEToplevel* toplevel, gboolean fullscreen)
{
	auto* self = VOS_TOPLEVEL(toplevel);
	// The exit sequence asks twice (Escape, then WebKit's own exit);
	// only the edge reaches the host.
	bool isFullscreen = (wpe_toplevel_get_state(toplevel)
		& WPE_TOPLEVEL_STATE_FULLSCREEN) != 0;
	if (isFullscreen == (fullscreen != FALSE))
		return TRUE;

	if (self->host == nullptr || !self->host->onFullscreenRequest)
		return FALSE;
	self->host->onFullscreenRequest(fullscreen != FALSE);
	return TRUE;
}


static void
vos_toplevel_constructed(GObject* object)
{
	G_OBJECT_CLASS(vos_toplevel_parent_class)->constructed(object);

	// The host window is always live; report the toplevel active so
	// pages see the window as focused once the view takes focus.
	wpe_toplevel_state_changed(WPE_TOPLEVEL(object), WPE_TOPLEVEL_STATE_ACTIVE);
}


static void
vos_toplevel_class_init(VosToplevelClass* toplevelClass)
{
	GObjectClass* objectClass = G_OBJECT_CLASS(toplevelClass);
	objectClass->constructed = vos_toplevel_constructed;

	WPEToplevelClass* wpeToplevelClass = WPE_TOPLEVEL_CLASS(toplevelClass);
	wpeToplevelClass->resize = vos_toplevel_resize;
	wpeToplevelClass->set_fullscreen = vos_toplevel_set_fullscreen;
}


static void
vos_toplevel_init(VosToplevel* toplevel)
{
	toplevel->host = nullptr;
}


//	#pragma mark - VosDisplay


//	#pragma mark - dma-buf formats


// The GPU app_server shows, picked like the GL stack picks it.
static bool
_vos_select_device(gl_device* device)
{
	gl_device devices[16];
	int count = gl_enumerate_devices(devices, 16);
	int policy = kGlPolicyAutomatic;
	gl_load_policy(&policy);

	const char* why = nullptr;
	int pick = gl_select_device(devices, count, policy,
		getenv("VOS_GL_DEVICE"), &why);
	if (pick < 0 || devices[pick].renderNode[0] == '\0')
		return false;

	*device = devices[pick];
	return true;
}


// Modifiers the GPU can render XRGB8888 into; linear is always usable.
// False when the node has no hardware driver: software GBM cannot allocate
// from an unprivileged client.
static bool
_vos_query_modifiers(const gl_device& device, std::vector<uint64_t>& modifiers)
{
	modifiers.push_back(0);

	PFNEGLQUERYDEVICESEXTPROC queryDevices
		= (PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress("eglQueryDevicesEXT");
	PFNEGLQUERYDEVICESTRINGEXTPROC queryString
		= (PFNEGLQUERYDEVICESTRINGEXTPROC)eglGetProcAddress(
			"eglQueryDeviceStringEXT");
	PFNEGLGETPLATFORMDISPLAYEXTPROC getDisplay
		= (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress(
			"eglGetPlatformDisplayEXT");
	if (queryDevices == nullptr || queryString == nullptr
			|| getDisplay == nullptr)
		return false;

	EGLDeviceEXT devices[16];
	EGLint count = 0;
	if (!queryDevices(16, devices, &count) || count <= 0)
		return false;

	for (EGLint i = 0; i < count; i++) {
		const char* render = queryString(devices[i],
			EGL_DRM_RENDER_NODE_FILE_EXT);
		const char* primary = queryString(devices[i],
			EGL_DRM_DEVICE_FILE_EXT);
		if ((render == nullptr || strcmp(render, device.renderNode) != 0)
				&& (primary == nullptr
					|| strcmp(primary, device.renderNode) != 0))
			continue;

		EGLDisplay display = getDisplay(EGL_PLATFORM_DEVICE_EXT, devices[i],
			nullptr);
		EGLint major, minor;
		if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major,
				&minor))
			return false;

		typedef const char* (*DriverNameProc)(EGLDisplay);
		DriverNameProc driverName = (DriverNameProc)eglGetProcAddress(
			"eglGetDisplayDriverName");
		const char* driver = driverName != nullptr ? driverName(display)
			: nullptr;
		if (driver != nullptr && strstr(driver, "swrast") != nullptr) {
			eglTerminate(display);
			return false;
		}

		const char* extensions = eglQueryString(display, EGL_EXTENSIONS);
		PFNEGLQUERYDMABUFMODIFIERSEXTPROC query
			= (PFNEGLQUERYDMABUFMODIFIERSEXTPROC)eglGetProcAddress(
				"eglQueryDmaBufModifiersEXT");
		if (extensions != nullptr && query != nullptr
				&& strstr(extensions, "EGL_EXT_image_dma_buf_import_modifiers")
					!= nullptr) {
			EGLuint64KHR all[64];
			EGLBoolean external[64];
			EGLint n = 0;
			if (query(display, (EGLint)kFormatXRGB8888, 64, all, external, &n)) {
				for (EGLint j = 0; j < n; j++) {
					// External-only cannot be rendered into; invalid is
					// no layout at all, and linear is already there.
					if (external[j] || all[j] == 0
							|| all[j] == 0x00ffffffffffffffULL)
						continue;
					modifiers.push_back(all[j]);
				}
			}
		}
		eglTerminate(display);
		return true;
	}
	return false;
}


static void
_vos_display_setup_dmabuf(VosDisplay* self)
{
	gl_device device;
	if (!_vos_select_device(&device))
		return;

	std::vector<uint64_t> modifiers;
	if (!_vos_query_modifiers(device, modifiers)) {
		log_info("VosDisplay: %s has no hardware driver, SHM only",
			device.renderNode);
		return;
	}

	self->drmDevice = wpe_drm_device_new(
		device.primaryNode[0] != '\0' ? device.primaryNode : nullptr,
		device.renderNode);
	WPEBufferFormatsBuilder* builder = wpe_buffer_formats_builder_new(
		self->drmDevice);
	wpe_buffer_formats_builder_append_group(builder, self->drmDevice,
		WPE_BUFFER_FORMAT_USAGE_RENDERING);
	for (size_t i = 0; i < modifiers.size(); i++) {
		wpe_buffer_formats_builder_append_format(builder, kFormatXRGB8888,
			modifiers[i]);
	}
	self->formats = wpe_buffer_formats_builder_end(builder);
	wpe_buffer_formats_builder_unref(builder);
	log_info("VosDisplay: dma-buf on %s, %zu modifiers", device.renderNode,
		modifiers.size());
}


static WPEDRMDevice*
vos_display_get_drm_device(WPEDisplay* display)
{
	return VOS_DISPLAY(display)->drmDevice;
}


static WPEBufferFormats*
vos_display_get_preferred_buffer_formats(WPEDisplay* display)
{
	return VOS_DISPLAY(display)->formats;
}


static void
vos_display_dispose(GObject* object)
{
	auto* self = VOS_DISPLAY(object);
	g_clear_pointer(&self->formats, g_object_unref);
	g_clear_pointer(&self->retiredFormats, g_object_unref);
	if (self->drmDevice != nullptr) {
		wpe_drm_device_unref(self->drmDevice);
		self->drmDevice = nullptr;
	}
	G_OBJECT_CLASS(vos_display_parent_class)->dispose(object);
}


static gboolean
vos_display_connect(WPEDisplay*, GError**)
{
	// The app_server surface is always there; there is nothing to
	// open and no socket to dial.
	return TRUE;
}


static VosPlatformHost* sNextHost = nullptr;


void
vos_display_set_next_host(VosPlatformHost* host)
{
	sNextHost = host;
}


static WPEView*
vos_display_create_view(WPEDisplay* display)
{
	auto* self = VOS_DISPLAY(display);
	auto* view = VOS_VIEW(g_object_new(VOS_TYPE_VIEW, "display", display,
		nullptr));
	VosPlatformHost* host = sNextHost != nullptr ? sNextHost : self->host;
	view->host = host;

	if (host != nullptr && host->compositor != nullptr
			&& host->compositor->Presenter() != nullptr) {
		WPEView* wpeView = WPE_VIEW(view);
		host->compositor->Presenter()->SetCallbacks(
			[wpeView](WPEBuffer* buffer) {
				wpe_view_buffer_rendered(wpeView, buffer);
			},
			[wpeView](WPEBuffer* buffer) {
				wpe_view_buffer_released(wpeView, buffer);
			});
	}
	return WPE_VIEW(view);
}


static WPEToplevel*
vos_display_create_toplevel(WPEDisplay* display, guint)
{
	auto* self = VOS_DISPLAY(display);
	auto* toplevel = VOS_TOPLEVEL(g_object_new(VOS_TYPE_TOPLEVEL, "display",
		display, nullptr));
	toplevel->host = sNextHost != nullptr ? sNextHost : self->host;
	return WPE_TOPLEVEL(toplevel);
}


static void
vos_display_class_init(VosDisplayClass* displayClass)
{
	G_OBJECT_CLASS(displayClass)->dispose = vos_display_dispose;

	WPEDisplayClass* wpeDisplayClass = WPE_DISPLAY_CLASS(displayClass);
	wpeDisplayClass->get_drm_device = vos_display_get_drm_device;
	wpeDisplayClass->get_preferred_buffer_formats
		= vos_display_get_preferred_buffer_formats;
	wpeDisplayClass->connect = vos_display_connect;
	wpeDisplayClass->create_view = vos_display_create_view;
	wpeDisplayClass->create_toplevel = vos_display_create_toplevel;
}


static void
vos_display_init(VosDisplay* display)
{
	display->host = nullptr;
	display->drmDevice = nullptr;
	display->formats = nullptr;
}


WPEDisplay*
vos_display_new(VosPlatformHost* host)
{
	auto* display = VOS_DISPLAY(g_object_new(VOS_TYPE_DISPLAY, nullptr));
	display->host = host;

	// Only with a swap chain to show them: otherwise the page renders
	// into SHM, which needs no negotiation.
	if (host != nullptr && host->compositor != nullptr
			&& host->compositor->Presenter() != nullptr
			&& host->compositor->Presenter()->IsReady())
		_vos_display_setup_dmabuf(display);

	log_info("VosDisplay: created (WPEPlatform, %s)",
		display->formats != nullptr ? "dma-buf, SHM" : "SHM");
	return WPE_DISPLAY(display);
}
