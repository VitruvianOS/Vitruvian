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
#include <string.h>

#include <Bitmap.h>
#include <GLView.h>
#include <Rect.h>

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
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
	fWidth(1),
	fHeight(1),
	fInitialized(false),
	fFboReady(false),
	fBitmap(NULL)
{
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
	printf("MesaSurfacelessRenderer: EGL %d.%d\n", major, minor);

	eglBindAPI(EGL_OPENGL_API);

	const EGLint configAttribs[] = {
		EGL_SURFACE_TYPE,    EGL_DONT_CARE,
		EGL_RED_SIZE,        8,
		EGL_GREEN_SIZE,      8,
		EGL_BLUE_SIZE,       8,
		EGL_ALPHA_SIZE,      8,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_NONE
	};
	EGLint numConfigs = 0;
	if (!eglChooseConfig(fDisplay, configAttribs, &fConfig, 1, &numConfigs)
			|| numConfigs == 0) {
		fprintf(stderr, "MesaSurfacelessRenderer: no EGL config\n");
		_Cleanup();
		return B_ERROR;
	}

	static const struct { int maj, min; } kVersions[] = {
		{4, 6}, {4, 5}, {4, 0}, {3, 3}, {3, 1}, {2, 0}, {0, 0}
	};
	for (int i = 0; kVersions[i].maj > 0; i++) {
		if (_TryCreateContext(kVersions[i].maj, kVersions[i].min))
			break;
	}
	if (fContext == EGL_NO_CONTEXT)
		fContext = eglCreateContext(fDisplay, fConfig, EGL_NO_CONTEXT, NULL);
	if (fContext == EGL_NO_CONTEXT) {
		fprintf(stderr, "MesaSurfacelessRenderer: cannot create GL context\n");
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


bool
MesaSurfacelessRenderer::_TryCreateContext(int major, int minor)
{
	const EGLint attribs[] = {
		EGL_CONTEXT_MAJOR_VERSION, major,
		EGL_CONTEXT_MINOR_VERSION, minor,
		EGL_CONTEXT_OPENGL_PROFILE_MASK,
			EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT,
		EGL_NONE
	};
	fContext = eglCreateContext(fDisplay, fConfig, EGL_NO_CONTEXT, attribs);
	if (fContext != EGL_NO_CONTEXT) {
		printf("MesaSurfacelessRenderer: GL %d.%d context\n", major, minor);
		return true;
	}
	return false;
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
		// BBitmap B_RGBA32 is BGRA byte order on Haiku.
		glPixelStorei(GL_PACK_ALIGNMENT, 4);
		glPixelStorei(GL_PACK_ROW_LENGTH,
			(GLint)(fBitmap->BytesPerRow() / 4));
		glReadPixels(0, 0, fWidth, fHeight, GL_BGRA, GL_UNSIGNED_BYTE,
			fBitmap->Bits());
		glPixelStorei(GL_PACK_ROW_LENGTH, 0);

		// glReadPixels returns bottom-up; flip in place.
		uint8_t* bits = (uint8_t*)fBitmap->Bits();
		uint32_t bpr  = (uint32_t)fBitmap->BytesPerRow();
		for (int32 y = 0; y < fHeight / 2; y++) {
			uint8_t* a = bits + y * bpr;
			uint8_t* b = bits + (fHeight - 1 - y) * bpr;
			for (uint32_t x = 0; x < bpr; x++) {
				uint8_t t = a[x]; a[x] = b[x]; b[x] = t;
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
	if (!fInitialized || !dest)
		return B_ERROR;

	LockGL();
	BRect bounds = dest->Bounds();
	int32 w = (int32)(bounds.Width()  + 1);
	int32 h = (int32)(bounds.Height() + 1);
	glReadPixels((GLint)source.x, (GLint)source.y, (GLsizei)w, (GLsizei)h,
		GL_BGRA, GL_UNSIGNED_BYTE, dest->Bits());
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
