/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif

#include "MesaSurfacelessRenderer.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <Bitmap.h>
#include <FrameRing.h>
#include <GLView.h>
#include <OS.h>
#include <Screen.h>
#include <Window.h>
#include <Rect.h>

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

#ifndef EGL_OPENGL_ES3_BIT_KHR
#define EGL_OPENGL_ES3_BIT_KHR 0x00000040
#endif


MesaSurfacelessRenderer::MesaSurfacelessRenderer(BGLView* view, ulong options,
	BGLDispatcher* dispatcher)
	:
	BGLRenderer(view, options, dispatcher),
	fDeviceWhy("no device"),
	fZinkMode(kGlZinkNative),
	fZinkWhy("native"),
	fDisplay(EGL_NO_DISPLAY),
	fContext(EGL_NO_CONTEXT),
	fConfig(NULL),
	fFbo(0),
	fColorRb(0),
	fDepthRb(0),
	fIsGles(false),
	fContextMajor(0),
	fContextMinor(0),
	fDebug(false),
	fFirstFrameDumped(false),
	fReadBuffer(NULL),
	fReadBufferSize(0),
	fWidth(1),
	fHeight(1),
	fInitialized(false),
	fFboReady(false),
	fRing(NULL),
	fLatest(-1),
	fDrawPending(false),
	fHeldSlot(-1),
	fLastSwap(0),
	fFramePeriod(0)
{
	memset(&fDevice, 0, sizeof(fDevice));
	fDebug = getenv("VOS_GL_DEBUG") != NULL;

	for (int32 i = 0; i < kSlotCount; i++)
		fSlots[i] = NULL;
	fRing = new(std::nothrow) BPrivate::FrameRing;
	if (fRing != NULL && fRing->SetSlotCount(kSlotCount) != B_OK) {
		fRing->Release();
		fRing = NULL;
	}

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
		fprintf(stderr, "MesaSurfacelessRenderer: initialization failed\n");
}


MesaSurfacelessRenderer::~MesaSurfacelessRenderer()
{
	_Cleanup();
	pthread_mutex_destroy(&fContextLock);
}


status_t
MesaSurfacelessRenderer::_ChooseEglConfig()
{
	// Prefer desktop GL; GLES keeps a GLES-only stack working.
	static const EGLint kRenderable[] = {
		EGL_OPENGL_BIT,
		EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT_KHR
	};

	for (int api = 0; api < 2; api++) {
		if (api == 0)
			eglBindAPI(EGL_OPENGL_API);
		else
			eglBindAPI(EGL_OPENGL_ES_API);

		const EGLint configAttribs[] = {
			EGL_SURFACE_TYPE,    EGL_DONT_CARE,
			EGL_RED_SIZE,        8,
			EGL_GREEN_SIZE,      8,
			EGL_BLUE_SIZE,       8,
			EGL_ALPHA_SIZE,      8,
			EGL_RENDERABLE_TYPE, kRenderable[api],
			EGL_NONE
		};
		EGLint numConfigs = 0;
		if (eglChooseConfig(fDisplay, configAttribs, &fConfig, 1,
				&numConfigs) && numConfigs > 0) {
			fIsGles = api == 1;
			return B_OK;
		}
	}

	fprintf(stderr, "MesaSurfacelessRenderer: no EGL config\n");
	return B_ERROR;
}


bool
MesaSurfacelessRenderer::_TryCreateContext(int major, int minor)
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
		if (!eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
				fContext)) {
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
MesaSurfacelessRenderer::_CreateContext()
{
	// BGLView clients may use fixed-function GL: take the highest
	// compatibility context.
	if (!fIsGles) {
		static const struct { int maj, min; } kVersions[] = {
			{4, 6}, {4, 5}, {4, 3}, {4, 0}, {3, 3},
			{3, 2}, {3, 1}, {3, 0}, {2, 1}, {0, 0}
		};
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

	fprintf(stderr, "MesaSurfacelessRenderer: cannot create GL context\n");
	return B_ERROR;
}


// Context must be current.
void
MesaSurfacelessRenderer::_LogGlStrings()
{
	const char* eglVendor = eglQueryString(fDisplay, EGL_VENDOR);
	const char* eglVersion = eglQueryString(fDisplay, EGL_VERSION);
	const char* glVendor = (const char*)glGetString(GL_VENDOR);
	const char* glRenderer = (const char*)glGetString(GL_RENDERER);
	const char* glVersion = (const char*)glGetString(GL_VERSION);
	const char* extensions = (const char*)glGetString(GL_EXTENSIONS);
	bool hasCompat = extensions != NULL
		&& strstr(extensions, "GL_ARB_compatibility") != NULL;

	printf("MesaSurfacelessRenderer: EGL %s vendor=%s\n",
		eglVersion != NULL ? eglVersion : "?",
		eglVendor != NULL ? eglVendor : "?");
	printf("MesaSurfacelessRenderer: GL %s vendor=%s renderer=%s (%s%s)\n",
		glVersion != NULL ? glVersion : "?",
		glVendor != NULL ? glVendor : "?",
		glRenderer != NULL ? glRenderer : "?",
		fIsGles ? "GLES" : "desktop GL",
		hasCompat ? ", ARB_compatibility" : "");

	syslog(LOG_INFO, "MesaSurfacelessRenderer: EGL %s vendor=%s",
		eglVersion != NULL ? eglVersion : "?",
		eglVendor != NULL ? eglVendor : "?");
	syslog(LOG_INFO, "MesaSurfacelessRenderer: GL %s renderer=%s api=%s "
		"arb_compatibility=%s", glVersion != NULL ? glVersion : "?",
		glRenderer != NULL ? glRenderer : "?",
		fIsGles ? "GLES" : "desktop", hasCompat ? "yes" : "no");
}


status_t
MesaSurfacelessRenderer::_SelectDevice()
{
	gl_device devices[16];
	int count = gl_enumerate_devices(devices, 16);
	int policy = kGlPolicyAutomatic;
	gl_load_policy(&policy);

	const char* override = getenv("VOS_GL_DEVICE");
	const char* why = NULL;
	int pick = gl_select_device(devices, count, policy, override, &why);
	if (pick < 0)
		return B_ERROR;

	fDevice = devices[pick];
	fDeviceWhy = why;
	printf("MesaSurfacelessRenderer: selected GPU %s (%s): %s\n",
		fDevice.renderNode[0] != '\0' ? fDevice.renderNode
			: fDevice.primaryNode,
		fDevice.driver, why);

	// Settle native versus zink before Mesa loads a driver; a quirk
	// row may force zink for this GPU.
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


// Bind the EGL display to the selected GPU through
// EGL_EXT_device_enumeration + EGL_EXT_platform_device. Returns
// EGL_NO_DISPLAY when the extensions are missing or the device is not
// among the enumerated ones; the caller falls back to surfaceless.
EGLDisplay
MesaSurfacelessRenderer::_BindDeviceDisplay()
{
	const char* extensions = eglQueryString(EGL_NO_DISPLAY,
		EGL_EXTENSIONS);
	if (extensions == NULL
			|| strstr(extensions, "EGL_EXT_device_enumeration") == NULL
			|| strstr(extensions, "EGL_EXT_platform_device") == NULL)
		return EGL_NO_DISPLAY;

	PFNEGLQUERYDEVICESEXTPROC queryDevices
		= (PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress(
			"eglQueryDevicesEXT");
	PFNEGLQUERYDEVICESTRINGEXTPROC queryDeviceString
		= (PFNEGLQUERYDEVICESTRINGEXTPROC)eglGetProcAddress(
			"eglQueryDeviceStringEXT");
	PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplay
		= (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress(
			"eglGetPlatformDisplayEXT");
	if (queryDevices == NULL || queryDeviceString == NULL
			|| getPlatformDisplay == NULL)
		return EGL_NO_DISPLAY;

	EGLDeviceEXT eglDevices[16];
	EGLint deviceCount = 0;
	if (!queryDevices(16, eglDevices, &deviceCount) || deviceCount <= 0)
		return EGL_NO_DISPLAY;

	const char* want = fDevice.renderNode[0] != '\0'
		? fDevice.renderNode : fDevice.primaryNode;
	for (EGLint i = 0; i < deviceCount; i++) {
		const char* file = queryDeviceString(eglDevices[i],
			EGL_DRM_DEVICE_FILE_EXT);
		if (file == NULL || strcmp(file, want) != 0)
			continue;

		EGLDisplay display = getPlatformDisplay(EGL_PLATFORM_DEVICE_EXT,
			eglDevices[i], NULL);
		if (display != EGL_NO_DISPLAY) {
			printf("MesaSurfacelessRenderer: bound EGL to %s\n",
				file);
			return display;
		}
	}

	return EGL_NO_DISPLAY;
}


status_t
MesaSurfacelessRenderer::_Init()
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

	fprintf(stderr, "MesaSurfacelessRenderer: native path failed, "
		"retrying with zink\n");
	_Cleanup();
	setenv("MESA_LOADER_DRIVER_OVERRIDE", "zink", 1);
	fZinkMode = kGlZinkFallback;
	fZinkWhy = "zink fallback";
	return _TryInit();
}


status_t
MesaSurfacelessRenderer::_TryInit()
{
	// Selection is best-effort here: the surfaceless platform works
	// without a DRM device, so a failed pick only loses the policy.
	if (_SelectDevice() != B_OK) {
		fprintf(stderr, "MesaSurfacelessRenderer: no GPU selected, "
			"using the default device\n");
		gl_zink_quirk quirks[GL_DEVICE_MAX_QUIRKS];
		int quirkCount = gl_load_zink_quirk_table(quirks,
			GL_DEVICE_MAX_QUIRKS);
		gl_decide_zink(getenv("VOS_GL_ZINK"), quirks, quirkCount, 0, 0,
			NULL, false, &fZinkMode, &fZinkWhy);
		if (fZinkMode != kGlZinkNative)
			setenv("MESA_LOADER_DRIVER_OVERRIDE", "zink", 1);
	}

	// Prefer binding the selected GPU explicitly; fall back to the
	// platform surfaceless display when the EGL device extensions are
	// missing or do not know the node.
	fDisplay = _BindDeviceDisplay();

	PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplay =
		(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress(
			"eglGetPlatformDisplayEXT");

	if (fDisplay == EGL_NO_DISPLAY && getPlatformDisplay != NULL) {
		fDisplay = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
			EGL_DEFAULT_DISPLAY, NULL);
	}
	if (fDisplay == EGL_NO_DISPLAY)
		fDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (fDisplay == EGL_NO_DISPLAY) {
		fprintf(stderr, "MesaSurfacelessRenderer: eglGetDisplay failed\n");
		return B_ERROR;
	}

	EGLint major = 0, minor = 0;
	if (!eglInitialize(fDisplay, &major, &minor)) {
		fprintf(stderr, "MesaSurfacelessRenderer: eglInitialize failed\n");
		_Cleanup();
		return B_ERROR;
	}

	if (_ChooseEglConfig() != B_OK) {
		_Cleanup();
		return B_ERROR;
	}

	if (_CreateContext() != B_OK) {
		_Cleanup();
		return B_ERROR;
	}

	// Bind context with no draw/read surface (surfaceless), then create FBO.
	if (!eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, fContext)) {
		fprintf(stderr, "MesaSurfacelessRenderer: surfaceless makeCurrent "
			"failed: 0x%x\n", eglGetError());
		_Cleanup();
		return B_ERROR;
	}

	_LogGlStrings();
	_VerifyZink();
	_PublishStatus();

	_CreateFbo(fWidth, fHeight);

	eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

	if (!fFboReady) {
		_Cleanup();
		return B_ERROR;
	}

	fInitialized = true;
	printf("MesaSurfacelessRenderer: ready %dx%d\n", fWidth, fHeight);
	printf("MesaSurfacelessRenderer: driver path %s (%s) on %s (%s)\n",
		fZinkMode == kGlZinkNative ? "native" : "zink", fZinkWhy,
		fDevice.renderNode[0] != '\0' ? fDevice.renderNode
			: fDevice.primaryNode,
		fDeviceWhy);
	return B_OK;
}


// Context must be current. Zink was chosen but the GL stack reported
// something else: say so, and keep what we obtained either way.
void
MesaSurfacelessRenderer::_VerifyZink()
{
	if (fZinkMode == kGlZinkNative)
		return;

	const char* glRenderer = (const char*)glGetString(GL_RENDERER);
	if (glRenderer == NULL || strstr(glRenderer, "zink") == NULL) {
		fprintf(stderr, "MesaSurfacelessRenderer: zink was requested "
			"(%s) but GL_RENDERER is '%s'\n", fZinkWhy,
			glRenderer != NULL ? glRenderer : "?");
		syslog(LOG_ERR, "MesaSurfacelessRenderer: zink requested (%s), "
			"got renderer '%s'", fZinkWhy,
			glRenderer != NULL ? glRenderer : "?");
	}
}


void
MesaSurfacelessRenderer::_PublishStatus()
{
	const char* glRenderer = (const char*)glGetString(GL_RENDERER);
	gl_publish_gl_status(&fDevice, glRenderer, fZinkMode, fZinkWhy,
		fDeviceWhy);
}


void
MesaSurfacelessRenderer::_CreateFbo(int32 width, int32 height)
{
	fFboReady = false;

	glGenFramebuffers(1, &fFbo);
	glGenRenderbuffers(1, &fColorRb);
	glGenRenderbuffers(1, &fDepthRb);

	glBindRenderbuffer(GL_RENDERBUFFER, fColorRb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);

	glBindRenderbuffer(GL_RENDERBUFFER, fDepthRb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);

	glBindFramebuffer(GL_FRAMEBUFFER, fFbo);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_RENDERBUFFER, fColorRb);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
		GL_RENDERBUFFER, fDepthRb);

	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		fprintf(stderr, "MesaSurfacelessRenderer: FBO incomplete 0x%x\n",
			status);
		_DestroyFbo();
		return;
	}

	glViewport(0, 0, width, height);
	fFboReady = true;
}


void
MesaSurfacelessRenderer::_DestroyFbo()
{
	if (fFbo)      { glDeleteFramebuffers(1, &fFbo);      fFbo = 0; }
	if (fColorRb)  { glDeleteRenderbuffers(1, &fColorRb); fColorRb = 0; }
	if (fDepthRb)  { glDeleteRenderbuffers(1, &fDepthRb); fDepthRb = 0; }
	fFboReady = false;
}


void
MesaSurfacelessRenderer::_Cleanup()
{
	if (fDisplay != EGL_NO_DISPLAY) {
		if (fContext != EGL_NO_CONTEXT && (fFbo || fColorRb || fDepthRb)) {
			eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, fContext);
			_DestroyFbo();
			eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
				EGL_NO_CONTEXT);
		}
		if (fContext != EGL_NO_CONTEXT) {
			eglDestroyContext(fDisplay, fContext);
			fContext = EGL_NO_CONTEXT;
		}
		eglTerminate(fDisplay);
		fDisplay = EGL_NO_DISPLAY;
	}
	for (int32 i = 0; i < kSlotCount; i++) {
		delete fSlots[i];
		fSlots[i] = NULL;
	}
	fHeldSlot = -1;
	fLatest = -1;
	if (fRing != NULL) {
		fRing->Release();
		fRing = NULL;
	}
	free(fReadBuffer);
	fReadBuffer = NULL;
	fReadBufferSize = 0;
}


void
MesaSurfacelessRenderer::LockGL()
{
	pthread_mutex_lock(&fContextLock);
	if (fInitialized) {
		eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, fContext);
		if (fFboReady)
			glBindFramebuffer(GL_FRAMEBUFFER, fFbo);
	}
}


void
MesaSurfacelessRenderer::UnlockGL()
{
	if (fInitialized)
		eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
			EGL_NO_CONTEXT);
	pthread_mutex_unlock(&fContextLock);
}


void
MesaSurfacelessRenderer::SwapBuffers(bool vSync)
{
	if (!fInitialized || !fFboReady)
		return;

	if (fRing == NULL)
		return;

	const size_t rowBytes = (size_t)fWidth * 4;
	const size_t readSize = rowBytes * fHeight;
	if (fReadBufferSize < readSize) {
		uint8* buffer = (uint8*)realloc(fReadBuffer, readSize);
		if (buffer != NULL) {
			fReadBuffer = buffer;
			fReadBufferSize = readSize;
		}
	}

	// The slot backing and these state transitions are where buffer layer
	// allocations and fences plug in.
	int32 slot = fRing->AcquireDecoderSlot();
	BBitmap* bitmap = slot >= 0 ? _PrepareSlot(slot) : NULL;
	if (slot >= 0 && bitmap == NULL)
		fRing->ReleaseSlot(slot);

	if (bitmap != NULL && fReadBufferSize >= readSize) {
		// glReadPixels waits for the frame and returns it bottom-up. A
		// packed read and one copy per row into the flipped position.
		// GLES has no GL_BGRA readback: read RGBA and swizzle.
		uint8* bits = (uint8*)bitmap->Bits();
		uint32 bpr  = (uint32)bitmap->BytesPerRow();
		GLenum readFormat = fIsGles ? GL_RGBA : GL_BGRA;

		glPixelStorei(GL_PACK_ALIGNMENT, 4);
		glReadPixels(0, 0, fWidth, fHeight, readFormat, GL_UNSIGNED_BYTE,
			fReadBuffer);

		for (int32 y = 0; y < fHeight; y++) {
			memcpy(bits + (fHeight - 1 - y) * bpr,
				fReadBuffer + y * rowBytes, rowBytes);
		}

		if (fIsGles) {
			for (int32 y = 0; y < fHeight; y++) {
				uint8* row = bits + y * bpr;
				for (int32 x = 0; x < fWidth; x++) {
					uint8 tmp = row[x * 4];
					row[x * 4] = row[x * 4 + 2];
					row[x * 4 + 2] = tmp;
				}
			}
		}

		if (!fFirstFrameDumped) {
			fFirstFrameDumped = true;
			if (fDebug) {
				const uint8* pixel = bits
					+ (fHeight / 2) * bpr + (fWidth / 2) * 4;
				fprintf(stderr, "VOS_GL_DEBUG: center pixel (BGRA) "
					"%02x %02x %02x %02x (FBO readback)\n",
					pixel[0], pixel[1], pixel[2], pixel[3]);
				syslog(LOG_INFO, "VOS_GL_DEBUG: center pixel (BGRA) "
					"%02x %02x %02x %02x (FBO readback)",
					pixel[0], pixel[1], pixel[2], pixel[3]);
			}
		}

		int64 sequence = 0;
		if (fRing->PublishSlot(slot, &sequence) == B_OK)
			fLatest = sequence * kSlotCount + slot;
	} else if (bitmap != NULL)
		fRing->ReleaseSlot(slot);

	// Latest frame wins: invalidate only when no draw is pending.
	BGLView* view = GLView();
	if (view != NULL && !fDrawPending.exchange(true)) {
		if (view->LockLooperWithTimeout(0) == B_OK) {
			view->Invalidate();
			view->UnlockLooper();
		} else
			fDrawPending = false;
	}

	if (vSync)
		_PaceToRefresh();
}


// Producer owns the slot, so it may swap the backing for a resized one.
BBitmap*
MesaSurfacelessRenderer::_PrepareSlot(int32 slot)
{
	BBitmap* bitmap = fSlots[slot];
	if (bitmap != NULL
			&& ((int32)(bitmap->Bounds().Width() + 1.0f) != fWidth
				|| (int32)(bitmap->Bounds().Height() + 1.0f) != fHeight)) {
		delete bitmap;
		bitmap = NULL;
	}
	if (bitmap == NULL) {
		bitmap = new(std::nothrow) BBitmap(
			BRect(0.0f, 0.0f, (float)(fWidth - 1), (float)(fHeight - 1)),
			B_RGBA32);
	}
	if (bitmap != NULL && !bitmap->IsValid()) {
		delete bitmap;
		bitmap = NULL;
	}
	fSlots[slot] = bitmap;
	return bitmap;
}


void
MesaSurfacelessRenderer::_PaceToRefresh()
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
MesaSurfacelessRenderer::GetGLProcAddress(const char* name)
{
	return (void*)eglGetProcAddress(name);
}


status_t
MesaSurfacelessRenderer::CopyPixelsOut(BPoint source, BBitmap* dest)
{
	if (!fInitialized || dest == NULL)
		return B_ERROR;

	LockGL();
	BRect bounds = dest->Bounds();
	int32 w = (int32)(bounds.Width()  + 1);
	int32 h = (int32)(bounds.Height() + 1);

	if (fIsGles) {
		// GLES glReadPixels has no GL_BGRA; read RGBA and swizzle.
		glReadPixels((GLint)source.x, (GLint)source.y, (GLsizei)w,
			(GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE, dest->Bits());
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
		glReadPixels((GLint)source.x, (GLint)source.y, (GLsizei)w,
			(GLsizei)h, GL_BGRA, GL_UNSIGNED_BYTE, dest->Bits());
	}
	UnlockGL();
	return B_OK;
}


status_t
MesaSurfacelessRenderer::CopyPixelsIn(BBitmap* source, BPoint dest)
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
MesaSurfacelessRenderer::Draw(BRect /*updateRect*/)
{
	BGLView* view = GLView();
	if (view == NULL)
		return;

	fDrawPending = false;
	if (fRing == NULL)
		return;

	// Claim the newest frame and keep it held, so an expose with nothing
	// new repaints it; the previous one goes back to the producer.
	int64 latest = fLatest;
	if (latest >= 0) {
		int32 slot = (int32)(latest % kSlotCount);
		if (slot != fHeldSlot
				&& fRing->ClaimSlot(slot, latest / kSlotCount)) {
			if (fHeldSlot >= 0)
				fRing->ReleaseSlot(fHeldSlot);
			fHeldSlot = slot;
		}
	}

	if (fHeldSlot >= 0 && fSlots[fHeldSlot] != NULL)
		view->DrawBitmap(fSlots[fHeldSlot], BPoint(0.0f, 0.0f));
}


void
MesaSurfacelessRenderer::AttachedToWindow()  {}
void
MesaSurfacelessRenderer::DetachedFromWindow() {}


void
MesaSurfacelessRenderer::FrameResized(float width, float height)
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

	eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, fContext);
	_DestroyFbo();
	fWidth  = newW;
	fHeight = newH;
	_CreateFbo(fWidth, fHeight);
	eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

	// Slots are rebuilt lazily by the producer at the new size.
	fLatest = -1;
	fHeldSlot = -1;
	if (fRing != NULL)
		fRing->Reset();

	pthread_mutex_unlock(&fContextLock);
}


BGLRenderer*
instantiate_gl_renderer(BGLView* view, ulong options, BGLDispatcher* dispatcher)
{
	MesaSurfacelessRenderer* renderer =
		new MesaSurfacelessRenderer(view, options, dispatcher);
	if (!renderer->IsInitialized()) {
		delete renderer;
		return NULL;
	}
	return renderer;
}
