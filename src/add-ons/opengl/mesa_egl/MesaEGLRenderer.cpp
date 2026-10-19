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
#include <syslog.h>
#include <unistd.h>

#include <Bitmap.h>
#include <GLView.h>
#include <Rect.h>

#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif

#ifndef EGL_OPENGL_ES3_BIT_KHR
#define EGL_OPENGL_ES3_BIT_KHR 0x00000040
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
	fGbmFormat(GBM_FORMAT_ARGB8888),
	fIsGles(false),
	fContextMajor(0),
	fContextMinor(0),
	fDebug(false),
	fFirstFrameDumped(false),
	fWidth(1),
	fHeight(1),
	fInitialized(false),
	fBitmap(NULL)
{
	fDevicePath[0] = '\0';
	fDebug = getenv("VOS_GL_DEBUG") != NULL;

	pthread_mutex_init(&fBitmapLock, NULL);

	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&fContextLock, &attr);
	pthread_mutexattr_destroy(&attr);

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
	pthread_mutex_destroy(&fBitmapLock);
	pthread_mutex_destroy(&fContextLock);
}


// A render node without a 3D driver opens but cannot allocate a buffer.
bool
MesaEGLRenderer::_ProbeGbmDevice()
{
	fGbmDevice = gbm_create_device(fDrmFd);
	if (fGbmDevice == NULL)
		return false;

	struct gbm_bo* bo = gbm_bo_create(fGbmDevice, 64, 64,
		GBM_FORMAT_ARGB8888, GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);
	if (bo == NULL) {
		gbm_device_destroy(fGbmDevice);
		fGbmDevice = NULL;
		return false;
	}

	gbm_bo_destroy(bo);
	return true;
}


status_t
MesaEGLRenderer::_OpenDrmDevice()
{
	// Try to reuse the DRM fd passed by janus
	const char* envFd = getenv("JANUS_DRM_FD");
	if (envFd != NULL && envFd[0] != '\0') {
		int inheritedFd = atoi(envFd);
		if (inheritedFd >= 0) {
			fDrmFd = dup(inheritedFd);
			if (fDrmFd >= 0 && _ProbeGbmDevice()) {
				fOwnsDrmFd = true;
				snprintf(fDevicePath, sizeof(fDevicePath),
					"JANUS_DRM_FD=%d", inheritedFd);
				return B_OK;
			}
			if (fDrmFd >= 0) {
				close(fDrmFd);
				fDrmFd = -1;
			}
		}
	}

	// On kmsro systems (RK3399) the GPU is on the render node.
	static const char* kDevices[] = {
		"/dev/dri/renderD128", "/dev/dri/renderD129",
		"/dev/dri/card0",      "/dev/dri/card1",
		NULL
	};
	for (int i = 0; kDevices[i] != NULL; i++) {
		fDrmFd = open(kDevices[i], O_RDWR | O_CLOEXEC);
		if (fDrmFd < 0)
			continue;
		if (_ProbeGbmDevice()) {
			fOwnsDrmFd = true;
			snprintf(fDevicePath, sizeof(fDevicePath), "%s",
				kDevices[i]);
			return B_OK;
		}
		close(fDrmFd);
		fDrmFd = -1;
	}

	fprintf(stderr, "MesaEGLRenderer: no usable DRM device\n");
	return B_ERROR;
}


status_t
MesaEGLRenderer::_ChooseEglConfig()
{
	EGLint configCount = 0;
	if (!eglGetConfigs(fDisplay, NULL, 0, &configCount) || configCount == 0)
		return B_ERROR;

	const EGLint kMaxConfigs = 64;
	if (configCount > kMaxConfigs)
		configCount = kMaxConfigs;

	EGLConfig configs[kMaxConfigs];
	EGLint found = 0;
	if (!eglGetConfigs(fDisplay, configs, configCount, &found))
		return B_ERROR;

	// The config's visual id must match the GBM format we map.
	static const uint32 kFormats[] = {
		GBM_FORMAT_ARGB8888, GBM_FORMAT_XRGB8888
	};
	static const EGLint kRenderable[] = {
		EGL_OPENGL_BIT,
		EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT_KHR
	};

	for (int api = 0; api < 2; api++) {
		if (api == 0)
			eglBindAPI(EGL_OPENGL_API);
		else
			eglBindAPI(EGL_OPENGL_ES_API);

		for (int f = 0; f < 2; f++) {
			for (EGLint i = 0; i < found; i++) {
				EGLint surfaceType = 0;
				EGLint red = 0, green = 0, blue = 0, depth = 0;
				EGLint renderable = 0;
				EGLint visualId = 0;

				eglGetConfigAttrib(fDisplay, configs[i],
					EGL_SURFACE_TYPE, &surfaceType);
				if ((surfaceType & EGL_WINDOW_BIT) == 0)
					continue;

				eglGetConfigAttrib(fDisplay, configs[i],
					EGL_RENDERABLE_TYPE, &renderable);
				if ((renderable & kRenderable[api]) == 0)
					continue;

				eglGetConfigAttrib(fDisplay, configs[i],
					EGL_RED_SIZE, &red);
				eglGetConfigAttrib(fDisplay, configs[i],
					EGL_GREEN_SIZE, &green);
				eglGetConfigAttrib(fDisplay, configs[i],
					EGL_BLUE_SIZE, &blue);
				eglGetConfigAttrib(fDisplay, configs[i],
					EGL_DEPTH_SIZE, &depth);
				if (red < 8 || green < 8 || blue < 8 || depth < 16)
					continue;

				eglGetConfigAttrib(fDisplay, configs[i],
					EGL_NATIVE_VISUAL_ID, &visualId);
				if ((uint32)visualId != kFormats[f])
					continue;

				fConfig = configs[i];
				fGbmFormat = kFormats[f];
				fIsGles = api == 1;
				return B_OK;
			}
		}
	}

	fprintf(stderr, "MesaEGLRenderer: no suitable EGL config\n");
	return B_ERROR;
}


bool
MesaEGLRenderer::_CreateGbmSurface(int32 width, int32 height)
{
	// SwapBuffers maps the front buffer: a tiled or AFBC one reads black.
	fGbmSurface = gbm_surface_create(fGbmDevice, (uint32)width,
		(uint32)height, fGbmFormat,
		GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);
	if (fGbmSurface == NULL) {
		fGbmSurface = gbm_surface_create(fGbmDevice, (uint32)width,
			(uint32)height, fGbmFormat, GBM_BO_USE_RENDERING);
	}

	return fGbmSurface != NULL;
}


bool
MesaEGLRenderer::_TryCreateContext(int major, int minor)
{
	EGLint attribs[7];
	int index = 0;
	attribs[index++] = EGL_CONTEXT_MAJOR_VERSION;
	attribs[index++] = major;
	attribs[index++] = EGL_CONTEXT_MINOR_VERSION;
	attribs[index++] = minor;
	// Compat from 3.2 up; Mesa refuses what it cannot honour.
	if (!fIsGles && (major > 3 || (major == 3 && minor >= 2))) {
		attribs[index++] = EGL_CONTEXT_OPENGL_PROFILE_MASK;
		attribs[index++] = EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT;
	}
	attribs[index] = EGL_NONE;

	fContext = eglCreateContext(fDisplay, fConfig, EGL_NO_CONTEXT, attribs);
	if (fContext == EGL_NO_CONTEXT)
		return false;

	eglQueryContext(fDisplay, fContext, EGL_CONTEXT_MAJOR_VERSION,
		&fContextMajor);
	eglQueryContext(fDisplay, fContext, EGL_CONTEXT_MINOR_VERSION,
		&fContextMinor);

	// 3.1 has no profiles: keep it only with ARB_compatibility.
	if (!fIsGles && major == 3 && minor == 1) {
		if (!eglMakeCurrent(fDisplay, fSurface, fSurface, fContext)) {
			eglDestroyContext(fDisplay, fContext);
			fContext = EGL_NO_CONTEXT;
			return false;
		}
		const char* extensions
			= (const char*)glGetString(GL_EXTENSIONS);
		bool hasCompat = extensions != NULL
			&& strstr(extensions, "GL_ARB_compatibility") != NULL;
		eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
			EGL_NO_CONTEXT);
		if (!hasCompat) {
			eglDestroyContext(fDisplay, fContext);
			fContext = EGL_NO_CONTEXT;
			return false;
		}
	}

	return true;
}


status_t
MesaEGLRenderer::_CreateContext()
{
	// BGLView clients may use fixed-function GL: take the highest
	// compatibility context.
	static const struct { int maj, min; } kVersions[] = {
		{4, 6}, {4, 5}, {4, 3}, {4, 0}, {3, 3},
		{3, 2}, {3, 1}, {3, 0}, {2, 1}, {0, 0}
	};

	if (!fIsGles) {
		for (int i = 0; kVersions[i].maj > 0; i++) {
			if (_TryCreateContext(kVersions[i].maj, kVersions[i].min))
				return B_OK;
		}
	} else {
		static const struct { int maj, min; } kEsVersions[] = {
			{3, 2}, {3, 1}, {3, 0}, {2, 0}, {0, 0}
		};
		for (int i = 0; kEsVersions[i].maj > 0; i++) {
			if (_TryCreateContext(kEsVersions[i].maj,
					kEsVersions[i].min))
				return B_OK;
		}
	}

	fprintf(stderr, "MesaEGLRenderer: cannot create GL context\n");
	return B_ERROR;
}


// Context must be current.
void
MesaEGLRenderer::_LogGlStrings()
{
	const char* eglVendor = eglQueryString(fDisplay, EGL_VENDOR);
	const char* eglVersion = eglQueryString(fDisplay, EGL_VERSION);
	const char* glVendor = (const char*)glGetString(GL_VENDOR);
	const char* glRenderer = (const char*)glGetString(GL_RENDERER);
	const char* glVersion = (const char*)glGetString(GL_VERSION);
	const char* extensions = (const char*)glGetString(GL_EXTENSIONS);
	bool hasCompat = extensions != NULL
		&& strstr(extensions, "GL_ARB_compatibility") != NULL;

	const char* formatName = fGbmFormat == GBM_FORMAT_XRGB8888
		? "XRGB8888" : "ARGB8888";

	printf("MesaEGLRenderer: EGL %s vendor=%s\n",
		eglVersion != NULL ? eglVersion : "?",
		eglVendor != NULL ? eglVendor : "?");
	printf("MesaEGLRenderer: GL %s renderer=%s (%s%s)\n",
		glVersion != NULL ? glVersion : "?",
		glRenderer != NULL ? glRenderer : "?",
		fIsGles ? "GLES" : "desktop GL",
		hasCompat ? ", ARB_compatibility" : "");
	printf("MesaEGLRenderer: device=%s gbm_format=%s\n", fDevicePath,
		formatName);

	syslog(LOG_INFO, "MesaEGLRenderer: EGL %s vendor=%s",
		eglVersion != NULL ? eglVersion : "?",
		eglVendor != NULL ? eglVendor : "?");
	syslog(LOG_INFO, "MesaEGLRenderer: GL %s renderer=%s api=%s "
		"arb_compatibility=%s", glVersion != NULL ? glVersion : "?",
		glRenderer != NULL ? glRenderer : "?",
		fIsGles ? "GLES" : "desktop", hasCompat ? "yes" : "no");
	syslog(LOG_INFO, "MesaEGLRenderer: device=%s gbm_format=%s "
		"context=%d.%d", fDevicePath, formatName, fContextMajor,
		fContextMinor);
}


status_t
MesaEGLRenderer::_Init()
{
	if (_OpenDrmDevice() != B_OK)
		return B_ERROR;

	// EGL display via GBM platform
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

	if (_ChooseEglConfig() != B_OK) {
		_Cleanup();
		return B_ERROR;
	}

	if (!_CreateGbmSurface(fWidth, fHeight)) {
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

	if (_CreateContext() != B_OK) {
		_Cleanup();
		return B_ERROR;
	}

	if (!eglMakeCurrent(fDisplay, fSurface, fSurface, fContext)) {
		fprintf(stderr, "MesaEGLRenderer: eglMakeCurrent failed: 0x%x\n",
			eglGetError());
		_Cleanup();
		return B_ERROR;
	}
	_LogGlStrings();
	eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
		EGL_NO_CONTEXT);

	fInitialized = true;
	printf("MesaEGLRenderer: ready %dx%d\n", fWidth, fHeight);
	return B_OK;
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
	pthread_mutex_lock(&fBitmapLock);
	delete fBitmap;
	fBitmap = NULL;
	pthread_mutex_unlock(&fBitmapLock);
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

	pthread_mutex_lock(&fBitmapLock);
	delete fBitmap;
	fBitmap = NULL;
	pthread_mutex_unlock(&fBitmapLock);

	if (_CreateGbmSurface(fWidth, fHeight)) {
		fSurface = eglCreateWindowSurface(fDisplay, fConfig,
			(EGLNativeWindowType)fGbmSurface, NULL);
	}

	if (fSurface != EGL_NO_SURFACE)
		eglMakeCurrent(fDisplay, fSurface, fSurface, fContext);
}


void
MesaEGLRenderer::LockGL()
{
	pthread_mutex_lock(&fContextLock);
	if (fInitialized && fSurface != EGL_NO_SURFACE)
		eglMakeCurrent(fDisplay, fSurface, fSurface, fContext);
}


void
MesaEGLRenderer::UnlockGL()
{
	if (fInitialized)
		eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
			EGL_NO_CONTEXT);
	pthread_mutex_unlock(&fContextLock);
}


void
MesaEGLRenderer::SwapBuffers(bool vSync)
{
	if (!fInitialized || fSurface == EGL_NO_SURFACE)
		return;

	pthread_mutex_lock(&fContextLock);

	if (eglGetCurrentContext() != fContext)
		eglMakeCurrent(fDisplay, fSurface, fSurface, fContext);

	eglSwapInterval(fDisplay, vSync ? 1 : 0);
	eglSwapBuffers(fDisplay, fSurface);

	// Lock the front GBM buffer object produced by eglSwapBuffers.
	struct gbm_bo* bo = gbm_surface_lock_front_buffer(fGbmSurface);
	if (bo == NULL) {
		fprintf(stderr, "MesaEGLRenderer::SwapBuffers: "
			"gbm_surface_lock_front_buffer failed\n");
		pthread_mutex_unlock(&fContextLock);
		return;
	}

	uint32_t stride = 0;
	void* mapData = NULL;
	void* pixels = gbm_bo_map(bo, 0, 0, (uint32_t)fWidth, (uint32_t)fHeight,
		GBM_BO_TRANSFER_READ, &stride, &mapData);
	if (pixels == NULL) {
		fprintf(stderr, "MesaEGLRenderer::SwapBuffers: gbm_bo_map failed\n");
		gbm_surface_release_buffer(fGbmSurface, bo);
		pthread_mutex_unlock(&fContextLock);
		return;
	}

	pthread_mutex_lock(&fBitmapLock);

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
		const uint8* src    = (const uint8*)pixels;
		uint8* dst          = (uint8*)fBitmap->Bits();
		uint32 dstBpr       = (uint32)fBitmap->BytesPerRow();
		// Force opaque alpha: zero alpha composites as black.
		for (int32 y = 0; y < fHeight; y++) {
			const uint32* src32 = (const uint32*)(src + y * stride);
			uint32* dst32 = (uint32*)(dst + y * dstBpr);
			for (int32 x = 0; x < fWidth; x++)
				dst32[x] = src32[x] | 0xff000000;
		}
	}

	if (!fFirstFrameDumped) {
		fFirstFrameDumped = true;
		unsigned long long modifier
			= (unsigned long long)gbm_bo_get_modifier(bo);
		syslog(LOG_INFO, "MesaEGLRenderer: front buffer stride=%u "
			"modifier=0x%llx", (unsigned)stride, modifier);
		if (fDebug && fBitmap != NULL && fBitmap->IsValid()) {
			const uint8* pixel = (const uint8*)fBitmap->Bits()
				+ (fHeight / 2) * (uint32)fBitmap->BytesPerRow()
				+ (fWidth / 2) * 4;
			fprintf(stderr, "VOS_GL_DEBUG: center pixel (BGRA) "
				"%02x %02x %02x %02x stride=%u modifier=0x%llx\n",
				pixel[0], pixel[1], pixel[2], pixel[3],
				(unsigned)stride, modifier);
			syslog(LOG_INFO, "VOS_GL_DEBUG: center pixel (BGRA) "
				"%02x %02x %02x %02x stride=%u modifier=0x%llx",
				pixel[0], pixel[1], pixel[2], pixel[3],
				(unsigned)stride, modifier);
		}
	}

	pthread_mutex_unlock(&fBitmapLock);

	gbm_bo_unmap(bo, mapData);
	gbm_surface_release_buffer(fGbmSurface, bo);

	pthread_mutex_unlock(&fContextLock);

	// Never block on the looper: its thread takes LockGL.
	BGLView* view = GLView();
	if (view != NULL && view->LockLooperWithTimeout(0) == B_OK) {
		view->Invalidate();
		view->UnlockLooper();
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
	if (!fInitialized || dest == NULL)
		return B_ERROR;

	LockGL();

	BRect bounds = dest->Bounds();
	int32 w = (int32)(bounds.Width()  + 1);
	int32 h = (int32)(bounds.Height() + 1);

	if (fIsGles) {
		// GLES glReadPixels has no GL_BGRA; read RGBA and swizzle.
		glReadPixels((GLint)source.x, (GLint)source.y,
			(GLsizei)w, (GLsizei)h,
			GL_RGBA, GL_UNSIGNED_BYTE, dest->Bits());
		uint8* bits = (uint8*)dest->Bits();
		uint32 bpr = (uint32)dest->BytesPerRow();
		for (int32 y = 0; y < h; y++) {
			uint8* row = bits + y * bpr;
			for (int32 x = 0; x < w; x++) {
				uint8 tmp = row[x * 4];
				row[x * 4] = row[x * 4 + 2];
				row[x * 4 + 2] = tmp;
			}
		}
	} else {
		glReadPixels((GLint)source.x, (GLint)source.y,
			(GLsizei)w, (GLsizei)h,
			GL_BGRA, GL_UNSIGNED_BYTE, dest->Bits());
	}

	UnlockGL();
	return B_OK;
}


status_t
MesaEGLRenderer::CopyPixelsIn(BBitmap* source, BPoint dest)
{
	if (!fInitialized || source == NULL)
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
	// Looper thread: blit the bitmap captured by SwapBuffers.
	BGLView* view = GLView();
	if (view == NULL)
		return;

	pthread_mutex_lock(&fBitmapLock);
	if (fBitmap != NULL && fBitmap->IsValid())
		view->DrawBitmap(fBitmap, BPoint(0.0f, 0.0f));
	pthread_mutex_unlock(&fBitmapLock);
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

	pthread_mutex_lock(&fContextLock);
	_RecreateSurface(newW, newH);
	pthread_mutex_unlock(&fContextLock);
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
