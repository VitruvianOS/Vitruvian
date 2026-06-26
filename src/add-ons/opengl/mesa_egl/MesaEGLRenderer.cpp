/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 *
 * Mesa EGL + GBM renderer for BGLView.
 */

#include "MesaEGLRenderer.h"

#include <errno.h>
#include <fcntl.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <Bitmap.h>
#include <GLView.h>
#include <Rect.h>

#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif


MesaEGLRenderer::MesaEGLRenderer(BGLView* view, ulong options,
	BGLDispatcher* dispatcher)
	:
	BGLRenderer(view, options, dispatcher),
	fDrmFd(-1),
	fOwnsDrmFd(false),
	fGbmDevice(NULL),
	fGbmSurface(NULL),
	fDisplay(EGL_NO_DISPLAY),
	fSurface(EGL_NO_SURFACE),
	fContext(EGL_NO_CONTEXT),
	fConfig(NULL),
	fWidth(1),
	fHeight(1),
	fInitialized(false),
	fBitmap(NULL)
{
	if (view != NULL) {
		BRect bounds = view->Bounds();
		fWidth  = (int32)(bounds.Width()  + 1.0f);
		fHeight = (int32)(bounds.Height() + 1.0f);
		if (fWidth  < 1) fWidth  = 1;
		if (fHeight < 1) fHeight = 1;
	}

	if (_Init() != B_OK)
		fprintf(stderr, "MesaEGLRenderer: initialization failed\n");
}


MesaEGLRenderer::~MesaEGLRenderer()
{
	_Cleanup();
}


status_t
MesaEGLRenderer::_Init()
{
	// Try to reuse the DRM fd passed by janus
	const char* envFd = getenv("JANUS_DRM_FD");
	if (envFd && envFd[0] != '\0') {
		int inheritedFd = atoi(envFd);
		if (inheritedFd >= 0) {
			fDrmFd = dup(inheritedFd);
			fOwnsDrmFd = true;
			printf("MesaEGLRenderer: using janus DRM fd (dup=%d)\n", fDrmFd);
		}
	}

	// If no inherited fd, try render nodes then card nodes
	if (fDrmFd < 0) {
		static const char* kDevices[] = {
			"/dev/dri/renderD128", "/dev/dri/renderD129",
			"/dev/dri/card0",      "/dev/dri/card1",
			NULL
		};
		for (int i = 0; kDevices[i]; i++) {
			fDrmFd = open(kDevices[i], O_RDWR | O_CLOEXEC);
			if (fDrmFd >= 0) {
				fOwnsDrmFd = true;
				printf("MesaEGLRenderer: opened %s\n", kDevices[i]);
				break;
			}
		}
	}

	if (fDrmFd < 0) {
		fprintf(stderr, "MesaEGLRenderer: no DRM device available\n");
		return B_ERROR;
	}

	// GBM device
	fGbmDevice = gbm_create_device(fDrmFd);
	if (!fGbmDevice) {
		fprintf(stderr, "MesaEGLRenderer: gbm_create_device failed\n");
		return B_ERROR;
	}

	// EGL display — prefer GBM platform via extension
	PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplay =
		(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress(
			"eglGetPlatformDisplayEXT");

	if (getPlatformDisplay != NULL) {
		fDisplay = getPlatformDisplay(EGL_PLATFORM_GBM_KHR,
			fGbmDevice, NULL);
	}
	if (fDisplay == EGL_NO_DISPLAY) {
		fDisplay = eglGetDisplay((EGLNativeDisplayType)fGbmDevice);
	}
	if (fDisplay == EGL_NO_DISPLAY) {
		fprintf(stderr, "MesaEGLRenderer: eglGetDisplay failed\n");
		_Cleanup();
		return B_ERROR;
	}

	EGLint major = 0, minor = 0;
	if (!eglInitialize(fDisplay, &major, &minor)) {
		fprintf(stderr, "MesaEGLRenderer: eglInitialize failed\n");
		_Cleanup();
		return B_ERROR;
	}
	printf("MesaEGLRenderer: EGL %d.%d\n", major, minor);

	// Choose EGL config
	const EGLint configAttribs[] = {
		EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
		EGL_RED_SIZE,        8,
		EGL_GREEN_SIZE,      8,
		EGL_BLUE_SIZE,       8,
		EGL_ALPHA_SIZE,      8,
		EGL_DEPTH_SIZE,      24,
		EGL_STENCIL_SIZE,    8,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_NONE
	};

	EGLint numConfigs = 0;
	eglBindAPI(EGL_OPENGL_API);

	if (!eglChooseConfig(fDisplay, configAttribs, &fConfig, 1, &numConfigs)
			|| numConfigs == 0) {
		// Retry without alpha/stencil
		const EGLint fallbackAttribs[] = {
			EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
			EGL_RED_SIZE,        8,
			EGL_GREEN_SIZE,      8,
			EGL_BLUE_SIZE,       8,
			EGL_DEPTH_SIZE,      16,
			EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
			EGL_NONE
		};
		if (!eglChooseConfig(fDisplay, fallbackAttribs, &fConfig, 1,
				&numConfigs) || numConfigs == 0) {
			fprintf(stderr, "MesaEGLRenderer: no suitable EGL config\n");
			_Cleanup();
			return B_ERROR;
		}
	}

	// GBM surface
	fGbmSurface = gbm_surface_create(fGbmDevice, fWidth, fHeight,
		GBM_FORMAT_ARGB8888, GBM_BO_USE_RENDERING);
	if (!fGbmSurface) {
		fprintf(stderr, "MesaEGLRenderer: gbm_surface_create failed\n");
		_Cleanup();
		return B_ERROR;
	}

	// EGL window surface
	fSurface = eglCreateWindowSurface(fDisplay, fConfig,
		(EGLNativeWindowType)fGbmSurface, NULL);
	if (fSurface == EGL_NO_SURFACE) {
		fprintf(stderr, "MesaEGLRenderer: eglCreateWindowSurface failed: 0x%x\n",
			eglGetError());
		_Cleanup();
		return B_ERROR;
	}

	// Try GL contexts highest→lowest version
	static const struct { int maj, min; } kVersions[] = {
		{4, 6}, {4, 5}, {4, 0}, {3, 3}, {3, 1}, {2, 0}, {0, 0}
	};
	for (int i = 0; kVersions[i].maj > 0; i++) {
		if (_TryCreateContext(kVersions[i].maj, kVersions[i].min))
			break;
	}
	if (fContext == EGL_NO_CONTEXT) {
		// Last resort: legacy context
		fContext = eglCreateContext(fDisplay, fConfig, EGL_NO_CONTEXT, NULL);
	}
	if (fContext == EGL_NO_CONTEXT) {
		fprintf(stderr, "MesaEGLRenderer: cannot create GL context\n");
		_Cleanup();
		return B_ERROR;
	}

	fInitialized = true;
	printf("MesaEGLRenderer: ready %dx%d\n", fWidth, fHeight);
	return B_OK;
}


bool
MesaEGLRenderer::_TryCreateContext(int major, int minor)
{
	const EGLint attribs[] = {
		EGL_CONTEXT_MAJOR_VERSION, major,
		EGL_CONTEXT_MINOR_VERSION, minor,
		EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
		EGL_NONE
	};
	fContext = eglCreateContext(fDisplay, fConfig, EGL_NO_CONTEXT, attribs);
	if (fContext != EGL_NO_CONTEXT) {
		printf("MesaEGLRenderer: GL %d.%d core context\n", major, minor);
		return true;
	}
	return false;
}


void
MesaEGLRenderer::_Cleanup()
{
	if (fDisplay != EGL_NO_DISPLAY) {
		eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
			EGL_NO_CONTEXT);
		if (fContext != EGL_NO_CONTEXT) {
			eglDestroyContext(fDisplay, fContext);
			fContext = EGL_NO_CONTEXT;
		}
		if (fSurface != EGL_NO_SURFACE) {
			eglDestroySurface(fDisplay, fSurface);
			fSurface = EGL_NO_SURFACE;
		}
		eglTerminate(fDisplay);
		fDisplay = EGL_NO_DISPLAY;
	}
	if (fGbmSurface) {
		gbm_surface_destroy(fGbmSurface);
		fGbmSurface = NULL;
	}
	if (fGbmDevice) {
		gbm_device_destroy(fGbmDevice);
		fGbmDevice = NULL;
	}
	if (fDrmFd >= 0 && fOwnsDrmFd) {
		close(fDrmFd);
		fDrmFd = -1;
	}
	delete fBitmap;
	fBitmap = NULL;
}


void
MesaEGLRenderer::_RecreateSurface(int32 width, int32 height)
{
	eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

	if (fSurface != EGL_NO_SURFACE) {
		eglDestroySurface(fDisplay, fSurface);
		fSurface = EGL_NO_SURFACE;
	}
	if (fGbmSurface) {
		gbm_surface_destroy(fGbmSurface);
		fGbmSurface = NULL;
	}

	fWidth  = width;
	fHeight = height;

	delete fBitmap;
	fBitmap = NULL;

	fGbmSurface = gbm_surface_create(fGbmDevice, fWidth, fHeight,
		GBM_FORMAT_ARGB8888, GBM_BO_USE_RENDERING);
	if (fGbmSurface) {
		fSurface = eglCreateWindowSurface(fDisplay, fConfig,
			(EGLNativeWindowType)fGbmSurface, NULL);
	}

	if (fSurface != EGL_NO_SURFACE)
		eglMakeCurrent(fDisplay, fSurface, fSurface, fContext);
}


void
MesaEGLRenderer::LockGL()
{
	if (fInitialized && fSurface != EGL_NO_SURFACE)
		eglMakeCurrent(fDisplay, fSurface, fSurface, fContext);
}


void
MesaEGLRenderer::UnlockGL()
{
	if (fInitialized)
		eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
			EGL_NO_CONTEXT);
}


void
MesaEGLRenderer::SwapBuffers(bool vSync)
{
	if (!fInitialized || fSurface == EGL_NO_SURFACE)
		return;

	eglSwapInterval(fDisplay, vSync ? 1 : 0);
	eglSwapBuffers(fDisplay, fSurface);

	// Lock the front GBM buffer object produced by eglSwapBuffers.
	struct gbm_bo* bo = gbm_surface_lock_front_buffer(fGbmSurface);
	if (bo == NULL) {
		fprintf(stderr, "MesaEGLRenderer::SwapBuffers: "
			"gbm_surface_lock_front_buffer failed\n");
		return;
	}

	uint32_t stride = 0;
	void* mapData = NULL;
	void* pixels = gbm_bo_map(bo, 0, 0, (uint32_t)fWidth, (uint32_t)fHeight,
		GBM_BO_TRANSFER_READ, &stride, &mapData);
	if (pixels == NULL) {
		fprintf(stderr, "MesaEGLRenderer::SwapBuffers: gbm_bo_map failed\n");
		gbm_surface_release_buffer(fGbmSurface, bo);
		return;
	}

	// Lazily allocate or reallocate the staging bitmap (B_RGBA32 = BGRA,
	// matches GBM_FORMAT_ARGB8888 in little-endian memory order).
	if (fBitmap == NULL
			|| (int32)(fBitmap->Bounds().Width() + 1.0f) != fWidth
			|| (int32)(fBitmap->Bounds().Height() + 1.0f) != fHeight) {
		delete fBitmap;
		fBitmap = new(std::nothrow) BBitmap(
			BRect(0.0f, 0.0f, (float)(fWidth - 1), (float)(fHeight - 1)),
			B_RGBA32);
	}

	if (fBitmap != NULL && fBitmap->IsValid()) {
		uint8_t* src    = (uint8_t*)pixels;
		uint8_t* dst    = (uint8_t*)fBitmap->Bits();
		uint32_t dstBpr = (uint32_t)fBitmap->BytesPerRow();
		uint32_t rowBytes = (uint32_t)fWidth * 4;
		for (int32 y = 0; y < fHeight; y++)
			memcpy(dst + y * dstBpr, src + y * stride, rowBytes);
	}

	gbm_bo_unmap(bo, mapData);
	gbm_surface_release_buffer(fGbmSurface, bo);

	if (fBitmap != NULL && fBitmap->IsValid()) {
		BGLView* view = GLView();
		if (view != NULL && view->LockLooper()) {
			view->DrawBitmap(fBitmap, BPoint(0.0f, 0.0f));
			view->UnlockLooper();
		}
	}
}


void*
MesaEGLRenderer::GetGLProcAddress(const char* name)
{
	return (void*)eglGetProcAddress(name);
}


status_t
MesaEGLRenderer::CopyPixelsOut(BPoint source, BBitmap* dest)
{
	if (!fInitialized || !dest)
		return B_ERROR;

	LockGL();

	BRect bounds = dest->Bounds();
	int32 w = (int32)(bounds.Width()  + 1);
	int32 h = (int32)(bounds.Height() + 1);

	glReadPixels((GLint)source.x, (GLint)source.y,
		(GLsizei)w, (GLsizei)h,
		GL_BGRA, GL_UNSIGNED_BYTE, dest->Bits());

	UnlockGL();
	return B_OK;
}


status_t
MesaEGLRenderer::CopyPixelsIn(BBitmap* source, BPoint dest)
{
	if (!fInitialized || !source)
		return B_ERROR;

	LockGL();

	BRect bounds = source->Bounds();
	int32 w = (int32)(bounds.Width()  + 1);
	int32 h = (int32)(bounds.Height() + 1);

	GLuint tex;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
		GL_BGRA, GL_UNSIGNED_BYTE, source->Bits());
	glDeleteTextures(1, &tex);

	UnlockGL();
	return B_OK;
}


void
MesaEGLRenderer::Draw(BRect /*updateRect*/)
{
	// Default no-op — user subclass overrides via BGLView::Draw()
}


void
MesaEGLRenderer::AttachedToWindow()
{
	// Context already created in constructor
}


void
MesaEGLRenderer::DetachedFromWindow()
{
	// Context kept alive until destructor
}


void
MesaEGLRenderer::FrameResized(float width, float height)
{
	if (!fInitialized)
		return;

	int32 newW = (int32)(width  + 1.0f);
	int32 newH = (int32)(height + 1.0f);
	if (newW < 1) newW = 1;
	if (newH < 1) newH = 1;

	if (newW == fWidth && newH == fHeight)
		return;

	_RecreateSurface(newW, newH);
}


// ---------------------------------------------------------------------------
// Add-on factory
// ---------------------------------------------------------------------------

BGLRenderer*
instantiate_gl_renderer(BGLView* view, ulong options, BGLDispatcher* dispatcher)
{
	MesaEGLRenderer* renderer = new MesaEGLRenderer(view, options, dispatcher);
	if (!renderer->IsInitialized()) {
		delete renderer;
		return NULL;
	}
	return renderer;
}
