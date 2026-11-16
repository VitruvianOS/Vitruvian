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

#include <GL/glext.h>

#include <Bitmap.h>
#include <GLView.h>
#include <Rect.h>
#include <Screen.h>
#include <Window.h>

#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif

#ifndef DRM_FORMAT_MOD_INVALID
#define DRM_FORMAT_MOD_INVALID 0x00ffffffffffffffULL
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
	fDeviceWhy("no device"),
	fZinkMode(kGlZinkNative),
	fZinkWhy("native"),
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
	fBitmap(NULL),
	fLastSwap(0),
	fFramePeriod(0),
	fChainState(kChainUntried),
	fChainCapable(false),
	fChainModifiers(false),
	fChainPending(-1),
	fChainSourceCount(0),
	fCreateImage(NULL),
	fDestroyImage(NULL),
	fImageTargetRenderbuffer(NULL),
	fGenFramebuffers(NULL),
	fDeleteFramebuffers(NULL),
	fBindFramebuffer(NULL),
	fFramebufferRenderbuffer(NULL),
	fCheckFramebufferStatus(NULL),
	fGenRenderbuffers(NULL),
	fDeleteRenderbuffers(NULL),
	fBindRenderbuffer(NULL),
	fBlitFramebuffer(NULL)
{
	memset(fChainTargets, 0, sizeof(fChainTargets));
	memset(fChainSources, 0, sizeof(fChainSources));

	fDevicePath[0] = '\0';
	memset(&fDevice, 0, sizeof(fDevice));
	fDebug = getenv("VOS_GL_DEBUG") != NULL;

	pthread_mutex_init(&fBitmapLock, NULL);
	pthread_mutex_init(&fChainLock, NULL);

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
	pthread_mutex_destroy(&fChainLock);
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


bool
MesaEGLRenderer::_TryOpenNode(const char* node)
{
	fDrmFd = open(node, O_RDWR | O_CLOEXEC);
	if (fDrmFd < 0)
		return false;
	if (_ProbeGbmDevice()) {
		fOwnsDrmFd = true;
		snprintf(fDevicePath, sizeof(fDevicePath), "%s", node);
		return true;
	}
	close(fDrmFd);
	fDrmFd = -1;
	return false;
}


status_t
MesaEGLRenderer::_SelectDevice()
{
	gl_device devices[16];
	int count = gl_enumerate_devices(devices, 16);
	int policy = kGlPolicyAutomatic;
	gl_load_policy(&policy);

	const char* override = getenv("VOS_GL_DEVICE");
	const char* why = NULL;
	int pick = gl_select_device(devices, count, policy, override, &why);
	if (pick < 0) {
		// No GPU table: janus may still hand us an fd in _OpenDrmDevice.
		if (getenv("JANUS_DRM_FD") != NULL)
			return B_OK;
		return B_ERROR;
	}

	fDevice = devices[pick];
	fDeviceWhy = why;
	printf("MesaEGLRenderer: selected GPU %s (%s): %s\n",
		fDevice.renderNode[0] != '\0' ? fDevice.renderNode
			: fDevice.primaryNode,
		fDevice.driver, why);

	// Settle native versus zink now that the device is known and
	// before any Mesa driver load; a quirk row may force zink.
	if (fZinkMode == kGlZinkNative) {
		gl_zink_quirk quirks[GL_DEVICE_MAX_QUIRKS];
		int quirkCount = gl_load_zink_quirk_table(quirks,
			GL_DEVICE_MAX_QUIRKS);
		gl_decide_zink(getenv("VOS_GL_ZINK"), quirks, quirkCount,
			fDevice.vendorId, fDevice.deviceId, fDevice.driver,
			false, &fZinkMode, &fZinkWhy);
		if (fZinkMode != kGlZinkNative)
			setenv("MESA_LOADER_DRIVER_OVERRIDE", "zink", 1);
	}

	return B_OK;
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

	const char* node = fDevice.renderNode[0] != '\0'
		? fDevice.renderNode : fDevice.primaryNode;
	if (node[0] != '\0' && _TryOpenNode(node))
		return B_OK;
	if (node[0] != '\0')
		fprintf(stderr, "MesaEGLRenderer: selected GPU %s not usable, "
			"falling back\n", node);

	// Fall back across the rest, but never wake a runtime suspended
	// dGPU the policy did not pick.
	gl_device devices[16];
	int count = gl_enumerate_devices(devices, 16);
	for (int i = 0; i < count; i++) {
		if (devices[i].runtimeSuspended)
			continue;
		if (devices[i].renderNode[0] != '\0'
				&& strcmp(devices[i].renderNode, node) != 0
				&& _TryOpenNode(devices[i].renderNode)) {
			fDevice = devices[i];
			fDeviceWhy = "fallback after selected GPU failed";
			return B_OK;
		}
		if (devices[i].primaryNode[0] != '\0'
				&& strcmp(devices[i].primaryNode, node) != 0
				&& _TryOpenNode(devices[i].primaryNode)) {
			fDevice = devices[i];
			fDeviceWhy = "fallback after selected GPU failed";
			return B_OK;
		}
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
	printf("MesaEGLRenderer: GL %s vendor=%s renderer=%s (%s%s)\n",
		glVersion != NULL ? glVersion : "?",
		glVendor != NULL ? glVendor : "?",
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
	// VOS_GL_ZINK=1 forces zink before any EGL call; the quirk-table
	// decision needs the device and runs in _TryInit once it is known.
	const char* zinkEnv = getenv("VOS_GL_ZINK");
	if (zinkEnv != NULL && strcmp(zinkEnv, "1") == 0) {
		fZinkMode = kGlZinkEnv;
		fZinkWhy = "env override";
		setenv("MESA_LOADER_DRIVER_OVERRIDE", "zink", 1);
	}

	status_t status = _TryInit();
	if (status == B_OK)
		return B_OK;

	// Native failed once: retry the same init with zink over Vulkan,
	// unless VOS_GL_ZINK=0 vetoes the fallback or zink already ran.
	if (fZinkMode != kGlZinkNative
			|| (zinkEnv != NULL && strcmp(zinkEnv, "0") == 0))
		return B_ERROR;

	fprintf(stderr, "MesaEGLRenderer: native path failed, retrying "
		"with zink\n");
	_Cleanup();
	setenv("MESA_LOADER_DRIVER_OVERRIDE", "zink", 1);
	fZinkMode = kGlZinkFallback;
	fZinkWhy = "zink fallback";
	return _TryInit();
}


status_t
MesaEGLRenderer::_TryInit()
{
	// Pick the GPU and settle the driver path before opening it: the
	// GBM probe loads Mesa's driver, and the zink override is an
	// in-process env var Mesa reads at driver load.
	if (_SelectDevice() != B_OK)
		return B_ERROR;

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
	_VerifyZink();
	_PublishStatus();
	fChainCapable = _ChainProbe();
	eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
		EGL_NO_CONTEXT);

	fInitialized = true;
	printf("MesaEGLRenderer: ready %dx%d\n", fWidth, fHeight);
	printf("MesaEGLRenderer: driver path %s (%s) on %s (%s)\n",
		fZinkMode == kGlZinkNative ? "native" : "zink", fZinkWhy,
		fDevicePath, fDeviceWhy);
	return B_OK;
}


// Context must be current. Zink was chosen but the GL stack reported
// something else: say so, and keep what we obtained either way.
void
MesaEGLRenderer::_VerifyZink()
{
	if (fZinkMode == kGlZinkNative)
		return;

	const char* glRenderer = (const char*)glGetString(GL_RENDERER);
	if (glRenderer == NULL || strstr(glRenderer, "zink") == NULL) {
		fprintf(stderr, "MesaEGLRenderer: zink was requested (%s) but "
			"GL_RENDERER is '%s'\n", fZinkWhy,
			glRenderer != NULL ? glRenderer : "?");
		syslog(LOG_ERR, "MesaEGLRenderer: zink requested (%s), got "
			"renderer '%s'", fZinkWhy,
			glRenderer != NULL ? glRenderer : "?");
	}
}


void
MesaEGLRenderer::_PublishStatus()
{
	const char* glRenderer = (const char*)glGetString(GL_RENDERER);
	gl_publish_gl_status(&fDevice, glRenderer, fZinkMode, fZinkWhy,
		fDeviceWhy);
}


void
MesaEGLRenderer::_Cleanup()
{
	if (fDisplay != EGL_NO_DISPLAY) {
		if (fContext != EGL_NO_CONTEXT && fSurface != EGL_NO_SURFACE
				&& fChainCapable
				&& eglMakeCurrent(fDisplay, fSurface, fSurface, fContext)) {
			_ChainDestroyTargets();
			_ChainDestroySources();
		}
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
	if (fChainCapable && fChainSourceCount > 0
			&& eglMakeCurrent(fDisplay, fSurface, fSurface, fContext))
		_ChainDestroySources();

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

	BGLView* chainView = GLView();
	if (fChainState == kChainUntried && fChainCapable && chainView != NULL
			&& chainView->LockLooperWithTimeout(20000) == B_OK) {
		_ChainStart();
		chainView->UnlockLooper();
	}

	// The buffer the frame goes to, taken before the context is held.
	int32 chainIndex = -1;
	if (fChainState == kChainOn) {
		_UpdateFramePeriod();
		pthread_mutex_lock(&fChainLock);
		chainIndex = fChainPending;
		if (chainIndex < 0) {
			const bigtime_t start = system_time();
			chainIndex = fChain.Acquire(fFramePeriod);
			if (chainIndex < 0)
				chainIndex = -2;
			if (chainIndex < 0 || system_time() - start > 20000)
				_ChainNote(chainIndex < 0 ? "no buffer, frame dropped"
					: "waited for a buffer", system_time() - start);
		}
		pthread_mutex_unlock(&fChainLock);
	}

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
		if (chainIndex >= 0) {
			pthread_mutex_lock(&fChainLock);
			fChainPending = chainIndex;
			pthread_mutex_unlock(&fChainLock);
		}
		return;
	}

	if (chainIndex != -1 && fChainState == kChainOn) {
		// -2: no buffer came free within a refresh, the frame is dropped.
		if (chainIndex >= 0 && !_ChainBlit(bo, chainIndex)) {
			pthread_mutex_lock(&fChainLock);
			fChainState = kChainOff;
			fChainPending = -1;
			pthread_mutex_unlock(&fChainLock);
			fprintf(stderr, "MesaEGLRenderer: swap chain failed, "
				"using the bitmap path\n");
			chainIndex = -1;
		}
		if (chainIndex != -1) {
			gbm_surface_release_buffer(fGbmSurface, bo);
			pthread_mutex_unlock(&fContextLock);
			if (chainIndex >= 0)
				_ChainPresent(chainIndex);
			if (vSync)
				_PaceToRefresh();
			return;
		}
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
		// the server can scan a GBM-backed bitmap out on a plane
		const char* overlay = getenv("VOS_OVERLAY_GL");
		uint32 flags = (overlay != NULL && atoi(overlay) == 1)
			? B_BITMAP_SUPPORTS_GBM : 0;
		fBitmap = new(std::nothrow) BBitmap(
			BRect(0.0f, 0.0f, (float)(fWidth - 1), (float)(fHeight - 1)),
			flags, B_RGBA32);
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

	if (vSync)
		_PaceToRefresh();
}


void
MesaEGLRenderer::_UpdateFramePeriod()
{
	if (fFramePeriod == 0) {
		fFramePeriod = 16667;
		BGLView* view = GLView();
		BWindow* window = view != NULL ? view->Window() : NULL;
		display_mode mode;
		bool haveMode = false;
		if (window != NULL) {
			BScreen screen(window);
			haveMode = screen.IsValid() && screen.GetMode(&mode) == B_OK;
		} else {
			BScreen screen(B_MAIN_SCREEN_ID);
			haveMode = screen.IsValid() && screen.GetMode(&mode) == B_OK;
		}
		if (haveMode && mode.timing.pixel_clock > 0) {
			uint64 total = (uint64)mode.timing.h_total * mode.timing.v_total;
			bigtime_t period = (bigtime_t)(total * 1000
				/ mode.timing.pixel_clock);
			if (period >= 2000 && period <= 100000)
				fFramePeriod = period;
		}
	}
}


// #pragma mark - swap chain


static bool
has_word(const char* list, const char* word)
{
	if (list == NULL)
		return false;

	const size_t length = strlen(word);
	for (const char* at = list; (at = strstr(at, word)) != NULL;
			at += length) {
		const bool start = at == list || at[-1] == ' ';
		const bool end = at[length] == ' ' || at[length] == '\0';
		if (start && end)
			return true;
	}
	return false;
}


// Context current.
bool
MesaEGLRenderer::_ChainProbe()
{
	if (!BPrivate::swap_chain_enabled() || fContextMajor < 3)
		return false;

	const char* eglExtensions = eglQueryString(fDisplay, EGL_EXTENSIONS);
	if (!has_word(eglExtensions, "EGL_KHR_image_base")
			|| !has_word(eglExtensions, "EGL_EXT_image_dma_buf_import"))
		return false;
	fChainModifiers = has_word(eglExtensions,
		"EGL_EXT_image_dma_buf_import_modifiers");

	bool hasImage = has_word((const char*)glGetString(GL_EXTENSIONS),
		"GL_OES_EGL_image");
	if (!hasImage) {
		// core profiles list them one by one
		typedef const GLubyte* (*GetStringi)(GLenum, GLuint);
		GetStringi getStringi = (GetStringi)eglGetProcAddress("glGetStringi");
		GLint count = 0;
		glGetIntegerv(GL_NUM_EXTENSIONS, &count);
		for (GLint i = 0; getStringi != NULL && i < count && !hasImage; i++) {
			hasImage = strcmp((const char*)getStringi(GL_EXTENSIONS, i),
				"GL_OES_EGL_image") == 0;
		}
	}
	if (!hasImage)
		return false;

	fCreateImage = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress(
		"eglCreateImageKHR");
	fDestroyImage = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress(
		"eglDestroyImageKHR");
	fImageTargetRenderbuffer = (void (*)(GLenum, void*))eglGetProcAddress(
		"glEGLImageTargetRenderbufferStorageOES");
	fGenFramebuffers = (void (*)(GLsizei, GLuint*))eglGetProcAddress(
		"glGenFramebuffers");
	fDeleteFramebuffers = (void (*)(GLsizei, const GLuint*))
		eglGetProcAddress("glDeleteFramebuffers");
	fBindFramebuffer = (void (*)(GLenum, GLuint))eglGetProcAddress(
		"glBindFramebuffer");
	fFramebufferRenderbuffer = (void (*)(GLenum, GLenum, GLenum, GLuint))
		eglGetProcAddress("glFramebufferRenderbuffer");
	fCheckFramebufferStatus = (GLenum (*)(GLenum))eglGetProcAddress(
		"glCheckFramebufferStatus");
	fGenRenderbuffers = (void (*)(GLsizei, GLuint*))eglGetProcAddress(
		"glGenRenderbuffers");
	fDeleteRenderbuffers = (void (*)(GLsizei, const GLuint*))
		eglGetProcAddress("glDeleteRenderbuffers");
	fBindRenderbuffer = (void (*)(GLenum, GLuint))eglGetProcAddress(
		"glBindRenderbuffer");
	fBlitFramebuffer = (void (*)(GLint, GLint, GLint, GLint, GLint, GLint,
		GLint, GLint, GLbitfield, GLenum))eglGetProcAddress(
		"glBlitFramebuffer");

	return fCreateImage != NULL && fDestroyImage != NULL
		&& fImageTargetRenderbuffer != NULL && fGenFramebuffers != NULL
		&& fDeleteFramebuffers != NULL && fBindFramebuffer != NULL
		&& fFramebufferRenderbuffer != NULL
		&& fCheckFramebufferStatus != NULL && fGenRenderbuffers != NULL
		&& fDeleteRenderbuffers != NULL && fBindRenderbuffer != NULL
		&& fBlitFramebuffer != NULL;
}


// The view's window is locked.
void
MesaEGLRenderer::_ChainStart()
{
	BGLView* view = GLView();
	if (fChainState != kChainUntried || view == NULL
			|| view->Window() == NULL)
		return;

	pthread_mutex_lock(&fChainLock);
	if (!fChainCapable) {
		fChainState = kChainOff;
	} else {
		const BPrivate::swap_chain_format format = {
			BPrivate::kSwapFormatXRGB8888, BPrivate::kSwapModifierLinear };
		status_t status = fChain.Create(view, fWidth, fHeight,
			BPrivate::kSwapChainMaxBuffers,
			BPrivate::kSwapUsageRender, &format, 1);
		fChainState = status == B_OK ? kChainOn : kChainOff;
		fChainPending = -1;
		if (status != B_OK) {
			fprintf(stderr, "MesaEGLRenderer: no swap chain (%s), "
				"using the bitmap path\n", strerror(status));
		} else
			printf("MesaEGLRenderer: swap chain %dx%d\n", fWidth, fHeight);
	}
	pthread_mutex_unlock(&fChainLock);
}


// The view's window is locked. Frees the chain and what was built on it.
void
MesaEGLRenderer::_ChainRetire()
{
	if (fChainState == kChainUntried)
		return;

	pthread_mutex_lock(&fContextLock);
	if (fSurface != EGL_NO_SURFACE
			&& eglMakeCurrent(fDisplay, fSurface, fSurface, fContext)) {
		_ChainDestroyTargets();
		_ChainDestroySources();
		eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
			EGL_NO_CONTEXT);
	}
	pthread_mutex_unlock(&fContextLock);

	pthread_mutex_lock(&fChainLock);
	fChain.Delete();
	fChainPending = -1;
	fChainState = kChainUntried;
	pthread_mutex_unlock(&fChainLock);
}


// Context current, fContextLock held.
bool
MesaEGLRenderer::_ChainBlit(struct gbm_bo* bo, int32 index)
{
	if (!_ChainTarget(index))
		return false;
	const int32 source = _ChainSource(bo);
	if (source < 0)
		return false;

	GLint readBinding = 0;
	GLint drawBinding = 0;
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readBinding);
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawBinding);
	const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
	if (scissor)
		glDisable(GL_SCISSOR_TEST);

	fBindFramebuffer(GL_READ_FRAMEBUFFER, fChainSources[source].framebuffer);
	fBindFramebuffer(GL_DRAW_FRAMEBUFFER, fChainTargets[index].framebuffer);
	fBlitFramebuffer(0, 0, fWidth, fHeight, 0, 0, fWidth, fHeight,
		GL_COLOR_BUFFER_BIT, GL_NEAREST);

	fBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)readBinding);
	fBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)drawBinding);
	if (scissor)
		glEnable(GL_SCISSOR_TEST);

	// The server reads the buffer next.
	glFinish();
	return true;
}


// At most one line a second.
void
MesaEGLRenderer::_ChainNote(const char* what, bigtime_t waited)
{
	static bigtime_t last = 0;
	const bigtime_t now = system_time();
	if (now - last < 1000000)
		return;

	last = now;
	fprintf(stderr, "MesaEGLRenderer: %s (%lld ms)\n", what,
		(long long)(waited / 1000));
}


// Hands the rendered buffer to the server. Takes no looper lock: the
// window thread may be waiting for the GL lock.
void
MesaEGLRenderer::_ChainPresent(int32 index)
{
	pthread_mutex_lock(&fChainLock);
	if (fChainState == kChainOn && fChain.IsValid())
		fChain.Present(index);
	fChainPending = -1;
	pthread_mutex_unlock(&fChainLock);
}


// Context current. Imports chain buffer index on first use.
bool
MesaEGLRenderer::_ChainTarget(int32 index)
{
	if (fChainTargets[index].framebuffer != 0)
		return true;

	pthread_mutex_lock(&fChainLock);
	BPrivate::swap_chain_buffer buffer;
	bool valid = fChain.BufferAt(index, &buffer) == B_OK;
	int fd = valid ? fChain.BufferFd(index) : -1;
	pthread_mutex_unlock(&fChainLock);
	if (fd < 0)
		return false;

	EGLImageKHR image = _ChainImport(fd, fWidth, fHeight, buffer.fourcc,
		buffer.offset, buffer.stride, buffer.modifier);
	close(fd);
	if (image == EGL_NO_IMAGE_KHR)
		return false;

	ChainImage& target = fChainTargets[index];
	if (!_ChainBuild(image, &target.renderbuffer, &target.framebuffer)) {
		fDestroyImage(fDisplay, image);
		return false;
	}

	target.image = image;
	return true;
}


// Context current. Index into fChainSources of the image of a front
// buffer of the gbm surface, or -1.
int32
MesaEGLRenderer::_ChainSource(struct gbm_bo* bo)
{
	for (int32 i = 0; i < fChainSourceCount; i++) {
		if (fChainSources[i].bo == bo)
			return i;
	}

	// The surface cycles a handful of buffers; anything else is a leak.
	if (fChainSourceCount == kChainSources)
		_ChainDestroySources();

	int fd = gbm_bo_get_fd(bo);
	if (fd < 0)
		return -1;

	static bool reported = false;
	if (!reported && gbm_bo_get_modifier(bo) != 0) {
		reported = true;
		fprintf(stderr, "MesaEGLRenderer: front buffer modifier %#llx\n",
			(unsigned long long)gbm_bo_get_modifier(bo));
	}

	EGLImageKHR image = _ChainImport(fd, (int32)gbm_bo_get_width(bo),
		(int32)gbm_bo_get_height(bo), gbm_bo_get_format(bo),
		gbm_bo_get_offset(bo, 0), gbm_bo_get_stride(bo),
		gbm_bo_get_modifier(bo));
	close(fd);
	if (image == EGL_NO_IMAGE_KHR)
		return -1;

	ChainImage& source = fChainSources[fChainSourceCount];
	if (!_ChainBuild(image, &source.renderbuffer, &source.framebuffer)) {
		fDestroyImage(fDisplay, image);
		return -1;
	}

	source.image = image;
	source.bo = bo;
	return fChainSourceCount++;
}


// Context current. A framebuffer with the image as its only attachment;
// what the client had bound is put back.
bool
MesaEGLRenderer::_ChainBuild(EGLImageKHR image, GLuint* _renderbuffer,
	GLuint* _framebuffer)
{
	GLint renderbufferBinding = 0;
	GLint readBinding = 0;
	GLint drawBinding = 0;
	glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbufferBinding);
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readBinding);
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawBinding);

	GLuint renderbuffer = 0;
	GLuint framebuffer = 0;
	fGenRenderbuffers(1, &renderbuffer);
	fBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
	fImageTargetRenderbuffer(GL_RENDERBUFFER, image);
	fGenFramebuffers(1, &framebuffer);
	fBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	fFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_RENDERBUFFER, renderbuffer);
	const bool complete
		= fCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

	fBindRenderbuffer(GL_RENDERBUFFER, (GLuint)renderbufferBinding);
	fBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)readBinding);
	fBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)drawBinding);

	if (!complete) {
		fDeleteFramebuffers(1, &framebuffer);
		fDeleteRenderbuffers(1, &renderbuffer);
		return false;
	}

	*_renderbuffer = renderbuffer;
	*_framebuffer = framebuffer;
	return true;
}


EGLImageKHR
MesaEGLRenderer::_ChainImport(int fd, int32 width, int32 height,
	uint32 fourcc, uint32 offset, uint32 stride, uint64 modifier)
{
	EGLint attributes[24];
	int n = 0;
	attributes[n++] = EGL_WIDTH;
	attributes[n++] = width;
	attributes[n++] = EGL_HEIGHT;
	attributes[n++] = height;
	attributes[n++] = EGL_LINUX_DRM_FOURCC_EXT;
	attributes[n++] = (EGLint)fourcc;
	attributes[n++] = EGL_DMA_BUF_PLANE0_FD_EXT;
	attributes[n++] = fd;
	attributes[n++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
	attributes[n++] = (EGLint)offset;
	attributes[n++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
	attributes[n++] = (EGLint)stride;
	if (fChainModifiers && modifier != DRM_FORMAT_MOD_INVALID) {
		attributes[n++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
		attributes[n++] = (EGLint)(modifier & 0xffffffff);
		attributes[n++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
		attributes[n++] = (EGLint)(modifier >> 32);
	}
	attributes[n] = EGL_NONE;

	return fCreateImage(fDisplay, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL,
		attributes);
}


void
MesaEGLRenderer::_ChainFree(EGLImageKHR* image, GLuint* renderbuffer,
	GLuint* framebuffer)
{
	if (*framebuffer != 0)
		fDeleteFramebuffers(1, framebuffer);
	if (*renderbuffer != 0)
		fDeleteRenderbuffers(1, renderbuffer);
	if (*image != EGL_NO_IMAGE_KHR && *image != NULL)
		fDestroyImage(fDisplay, *image);

	*framebuffer = 0;
	*renderbuffer = 0;
	*image = EGL_NO_IMAGE_KHR;
}


// Context current.
void
MesaEGLRenderer::_ChainDestroyTargets()
{
	for (int32 i = 0; i < BPrivate::kSwapChainMaxBuffers; i++) {
		ChainImage& target = fChainTargets[i];
		_ChainFree(&target.image, &target.renderbuffer, &target.framebuffer);
	}
}


// Context current.
void
MesaEGLRenderer::_ChainDestroySources()
{
	for (int32 i = 0; i < fChainSourceCount; i++) {
		ChainImage& source = fChainSources[i];
		_ChainFree(&source.image, &source.renderbuffer, &source.framebuffer);
		source.bo = NULL;
	}
	fChainSourceCount = 0;
}


void
MesaEGLRenderer::_PaceToRefresh()
{
	_UpdateFramePeriod();

	bigtime_t now = system_time();
	bigtime_t deadline = fLastSwap + fFramePeriod;
	if (now < deadline) {
		snooze_until(deadline, B_SYSTEM_TIMEBASE);
		now = deadline;
	}

	// No catch-up bursts when more than one period late.
	fLastSwap = (now - deadline > fFramePeriod) ? now : deadline;
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

	// The chain is on screen; a stale bitmap would fight it for the plane.
	if (fChainState == kChainOn)
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
	if (fInitialized && fChainCapable)
		_ChainStart();
}


void
MesaEGLRenderer::DetachedFromWindow()
{
	// Context kept alive until destructor
	if (fInitialized && fChainCapable)
		_ChainRetire();
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
	if (fChainState == kChainOn && eglMakeCurrent(fDisplay, fSurface,
			fSurface, fContext))
		_ChainDestroyTargets();
	_RecreateSurface(newW, newH);

	if (fChainState == kChainOn) {
		// The window is locked; the old buffers stay with the server
		// until the first frame of the new size.
		pthread_mutex_lock(&fChainLock);
		fChainPending = -1;
		if (fChain.Resize(newW, newH) != B_OK) {
			fprintf(stderr, "MesaEGLRenderer: swap chain resize failed, "
				"using the bitmap path\n");
			fChain.Delete();
			fChainState = kChainOff;
		}
		pthread_mutex_unlock(&fChainLock);
	}
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
