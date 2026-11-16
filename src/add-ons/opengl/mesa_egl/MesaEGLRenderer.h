/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */
#ifndef MESA_EGL_RENDERER_H
#define MESA_EGL_RENDERER_H

#include <GLRenderer.h>

#include <pthread.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>

#include <gbm.h>

#include <opengl/GlDeviceSelect.h>

#include <interface/SwapChain.h>

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
	status_t					_TryInit();
	status_t					_SelectDevice();
	status_t					_OpenDrmDevice();
	bool						_TryOpenNode(const char* node);
	bool						_ProbeGbmDevice();
	status_t					_ChooseEglConfig();
	status_t					_CreateContext();
	bool						_CreateGbmSurface(int32 width, int32 height);
	bool						_TryCreateContext(int major, int minor);
	void						_LogGlStrings();
	void						_VerifyZink();
	void						_PublishStatus();
	void						_PaceToRefresh();
	void						_Cleanup();
	void						_RecreateSurface(int32 width, int32 height);
	void						_UpdateFramePeriod();

	// VOS_SWAP_CHAIN=1: the GPU copies each frame into a buffer the server
	// scans out; nothing is read back. Any failure leaves the bitmap path.
	bool						_ChainProbe();
	void						_ChainStart();
	void						_ChainRetire();
	bool						_ChainBlit(struct gbm_bo* bo, int32 index);
	void						_ChainPresent(int32 index);
	void						_ChainNote(const char* what,
										bigtime_t waited);
	bool						_ChainTarget(int32 index);
	int32						_ChainSource(struct gbm_bo* bo);
	bool						_ChainBuild(EGLImageKHR image, GLuint* renderbuffer,
									GLuint* framebuffer);
	EGLImageKHR					_ChainImport(int fd, int32 width, int32 height,
									uint32 fourcc, uint32 offset,
									uint32 stride, uint64 modifier);
	void						_ChainDestroyTargets();
	void						_ChainDestroySources();
	void						_ChainFree(EGLImageKHR* image,
									GLuint* renderbuffer, GLuint* framebuffer);

private:
	int							fDrmFd;
	bool						fOwnsDrmFd;
	char						fDevicePath[128];
	gl_device					fDevice;
	const char*					fDeviceWhy;
	int							fZinkMode;
	const char*					fZinkWhy;
	struct gbm_device*			fGbmDevice;
	struct gbm_surface*			fGbmSurface;

	EGLDisplay					fDisplay;
	EGLSurface					fSurface;
	EGLContext					fContext;
	EGLConfig					fConfig;

	uint32						fGbmFormat;
	bool						fIsGles;
	int32						fContextMajor;
	int32						fContextMinor;
	bool						fDebug;
	bool						fFirstFrameDumped;

	int32						fWidth;
	int32						fHeight;
	bool						fInitialized;

	BBitmap*					fBitmap;
	bigtime_t					fLastSwap;
	bigtime_t					fFramePeriod;
	pthread_mutex_t				fBitmapLock;
	pthread_mutex_t				fContextLock;

	struct ChainImage {
		EGLImageKHR				image;
		GLuint					renderbuffer;
		GLuint					framebuffer;
		struct gbm_bo*			bo;
	};
	enum { kChainUntried, kChainOn, kChainOff };
	static const int32			kChainSources = 6;

	int32						fChainState;
	bool						fChainCapable;
	bool						fChainModifiers;
	BPrivate::SwapChain			fChain;
	pthread_mutex_t				fChainLock;
	// Acquired and rendered, not yet presented.
	int32						fChainPending;
	ChainImage					fChainTargets[BPrivate::kSwapChainMaxBuffers];
	ChainImage					fChainSources[kChainSources];
	int32						fChainSourceCount;

	PFNEGLCREATEIMAGEKHRPROC	fCreateImage;
	PFNEGLDESTROYIMAGEKHRPROC	fDestroyImage;
	void						(*fImageTargetRenderbuffer)(GLenum, void*);
	void						(*fGenFramebuffers)(GLsizei, GLuint*);
	void						(*fDeleteFramebuffers)(GLsizei, const GLuint*);
	void						(*fBindFramebuffer)(GLenum, GLuint);
	void						(*fFramebufferRenderbuffer)(GLenum, GLenum,
									GLenum, GLuint);
	GLenum						(*fCheckFramebufferStatus)(GLenum);
	void						(*fGenRenderbuffers)(GLsizei, GLuint*);
	void						(*fDeleteRenderbuffers)(GLsizei,
									const GLuint*);
	void						(*fBindRenderbuffer)(GLenum, GLuint);
	void						(*fBlitFramebuffer)(GLint, GLint, GLint, GLint,
									GLint, GLint, GLint, GLint, GLbitfield,
									GLenum);
};


extern "C" BGLRenderer* instantiate_gl_renderer(BGLView* view, ulong options,
	BGLDispatcher* dispatcher);

#endif // MESA_EGL_RENDERER_H
