/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */
#ifndef MESA_SURFACELESS_RENDERER_H
#define MESA_SURFACELESS_RENDERER_H

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif

#include <GLRenderer.h>

#include <pthread.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>

class BBitmap;


class MesaSurfacelessRenderer : public BGLRenderer {
public:
								MesaSurfacelessRenderer(BGLView* view,
									ulong options, BGLDispatcher* dispatcher);
	virtual						~MesaSurfacelessRenderer();

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
	void						_CreateFbo(int32 width, int32 height);
	void						_DestroyFbo();

private:
	EGLDisplay					fDisplay;
	EGLContext					fContext;
	EGLConfig					fConfig;

	GLuint						fFbo;
	GLuint						fColorRb;
	GLuint						fDepthRb;

	int32						fWidth;
	int32						fHeight;
	bool						fInitialized;
	bool						fFboReady;

	BBitmap*					fBitmap;
	pthread_mutex_t				fBitmapLock;
	pthread_mutex_t				fContextLock;
};


extern "C" BGLRenderer* instantiate_gl_renderer(BGLView* view, ulong options,
	BGLDispatcher* dispatcher);

#endif // MESA_SURFACELESS_RENDERER_H
