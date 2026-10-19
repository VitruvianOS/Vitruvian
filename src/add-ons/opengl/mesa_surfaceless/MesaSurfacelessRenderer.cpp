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
#include <GLView.h>
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
	fWidth(1),
	fHeight(1),
	fInitialized(false),
	fFboReady(false),
	fBitmap(NULL)
{
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
		fprintf(stderr, "MesaSurfacelessRenderer: initialization failed\n");
}


MesaSurfacelessRenderer::~MesaSurfacelessRenderer()
{
	_Cleanup();
	pthread_mutex_destroy(&fBitmapLock);
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
	printf("MesaSurfacelessRenderer: GL %s renderer=%s (%s%s)\n",
		glVersion != NULL ? glVersion : "?",
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
MesaSurfacelessRenderer::_Init()
{
	PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplay =
		(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress(
			"eglGetPlatformDisplayEXT");

	if (getPlatformDisplay != NULL) {
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

	_CreateFbo(fWidth, fHeight);

	eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

	if (!fFboReady) {
		_Cleanup();
		return B_ERROR;
	}

	fInitialized = true;
	printf("MesaSurfacelessRenderer: ready %dx%d\n", fWidth, fHeight);
	return B_OK;
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
	delete fBitmap;
	fBitmap = NULL;
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
MesaSurfacelessRenderer::SwapBuffers(bool /*vSync*/)
{
	if (!fInitialized || !fFboReady)
		return;

	glFinish();

	pthread_mutex_lock(&fBitmapLock);

	if (fBitmap == NULL
			|| (int32)(fBitmap->Bounds().Width() + 1.0f) != fWidth
			|| (int32)(fBitmap->Bounds().Height() + 1.0f) != fHeight) {
		delete fBitmap;
		fBitmap = new(std::nothrow) BBitmap(
			BRect(0.0f, 0.0f, (float)(fWidth - 1), (float)(fHeight - 1)),
			B_RGBA32);
	}

	if (fBitmap != NULL && fBitmap->IsValid()) {
		// GLES has no GL_BGRA readback: read RGBA and swizzle.
		uint8* bits = (uint8*)fBitmap->Bits();
		uint32 bpr  = (uint32)fBitmap->BytesPerRow();
		GLenum readFormat = fIsGles ? GL_RGBA : GL_BGRA;

		glPixelStorei(GL_PACK_ALIGNMENT, 4);
		if (!fIsGles || fContextMajor >= 3) {
			glPixelStorei(GL_PACK_ROW_LENGTH,
				(GLint)(bpr / 4));
			glReadPixels(0, 0, fWidth, fHeight, readFormat,
				GL_UNSIGNED_BYTE, bits);
			glPixelStorei(GL_PACK_ROW_LENGTH, 0);
		} else {
			// ES 2 has no GL_PACK_ROW_LENGTH; read row by row.
			uint8* row = (uint8*)malloc((size_t)fWidth * 4);
			if (row != NULL) {
				for (int32 y = 0; y < fHeight; y++) {
					glReadPixels(0, y, fWidth, 1, readFormat,
						GL_UNSIGNED_BYTE, row);
					memcpy(bits + y * bpr, row,
						(size_t)fWidth * 4);
				}
				free(row);
			}
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

		// glReadPixels returns bottom-up; flip in place.
		for (int32 y = 0; y < fHeight / 2; y++) {
			uint8_t* a = bits + y * bpr;
			uint8_t* b = bits + (fHeight - 1 - y) * bpr;
			for (uint32_t x = 0; x < bpr; x++) {
				uint8_t t = a[x]; a[x] = b[x]; b[x] = t;
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
	}

	pthread_mutex_unlock(&fBitmapLock);

	BGLView* view = GLView();
	if (view != NULL && view->LockLooperWithTimeout(0) == B_OK) {
		view->Invalidate();
		view->UnlockLooper();
	}
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

	pthread_mutex_lock(&fBitmapLock);
	if (fBitmap != NULL && fBitmap->IsValid())
		view->DrawBitmap(fBitmap, BPoint(0.0f, 0.0f));
	pthread_mutex_unlock(&fBitmapLock);
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

	pthread_mutex_lock(&fBitmapLock);
	delete fBitmap;
	fBitmap = NULL;
	pthread_mutex_unlock(&fBitmapLock);

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
