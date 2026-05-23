/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef MESA_EGL_RENDERER_H
#define MESA_EGL_RENDERER_H

#include <GLRenderer.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>

#include <gbm.h>

class BBitmap;
struct gbm_device;
struct gbm_surface;


class MesaEGLRenderer : public BGLRenderer {
public:
								MesaEGLRenderer(BGLView* view, ulong options,
									BGLDispatcher* dispatcher);
	virtual						~MesaEGLRenderer();

	bool						IsInitialized() const { return fInitialized; }

	virtual void				LockGL();
	virtual void				UnlockGL();
	virtual void				SwapBuffers(bool vSync = false);
	virtual void*				GetGLProcAddress(const char* name);
	virtual status_t			CopyPixelsOut(BPoint source, BBitmap* dest);
	virtual status_t			CopyPixelsIn(BBitmap* source, BPoint dest);
	virtual void				Draw(BRect updateRect);
	virtual void				AttachedToWindow();
	virtual void				DetachedFromWindow();
	virtual void				FrameResized(float width, float height);

private:
	status_t					_Init();
	bool						_TryCreateContext(int major, int minor);
	void						_Cleanup();
	void						_RecreateSurface(int32 width, int32 height);

private:
	int							fDrmFd;
	bool						fOwnsDrmFd;
	struct gbm_device*			fGbmDevice;
	struct gbm_surface*			fGbmSurface;

	EGLDisplay					fDisplay;
	EGLSurface					fSurface;
	EGLContext					fContext;
	EGLConfig					fConfig;

	int32						fWidth;
	int32						fHeight;
	bool						fInitialized;

	BBitmap*					fBitmap;
};


extern "C" BGLRenderer* instantiate_gl_renderer(BGLView* view, ulong options,
	BGLDispatcher* dispatcher);

#endif // MESA_EGL_RENDERER_H
