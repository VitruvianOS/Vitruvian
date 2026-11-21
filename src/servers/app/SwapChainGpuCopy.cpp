/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "SwapChainGpuCopy.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <Autolock.h>
#include <BufferAllocator.h>
#include <OS.h>

#ifdef HAVE_GBM
#	include <gbm.h>
#	include <EGL/egl.h>
#	include <EGL/eglext.h>
#	define GL_GLEXT_PROTOTYPES 1
#	include <GLES2/gl2.h>
#	include <GLES2/gl2ext.h>
#	include <linux/dma-buf.h>
#endif

using namespace BPrivate;

#include "ClientMemoryAllocator.h"
#include "ServerBitmap.h"


int
SwapChainGpuCopy::Mode()
{
#ifdef HAVE_GBM
	const char* value = getenv("VOS_SWAP_CHAIN_GPU_COPY");
	if (value == NULL || value[0] == '\0')
		return 2;
	return atoi(value) != 0 ? 1 : 0;
#else
	return 0;
#endif
}


#ifdef HAVE_GBM

static PFNEGLCREATEIMAGEKHRPROC sCreateImage;
static PFNEGLDESTROYIMAGEKHRPROC sDestroyImage;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC sImageTarget;
static PFNEGLCREATESYNCKHRPROC sCreateSync;
static PFNEGLCLIENTWAITSYNCKHRPROC sClientWait;
static PFNEGLDESTROYSYNCKHRPROC sDestroySync;
static PFNEGLDUPNATIVEFENCEFDANDROIDPROC sDupFence;


static EGLImageKHR
import_image(EGLDisplay display, int fd, int32 width, int32 height,
	uint32 stride, uint64 modifier, bool useModifier)
{
	EGLint attributes[32];
	int n = 0;
	attributes[n++] = EGL_WIDTH;
	attributes[n++] = width;
	attributes[n++] = EGL_HEIGHT;
	attributes[n++] = height;
	attributes[n++] = EGL_LINUX_DRM_FOURCC_EXT;
	attributes[n++] = GBM_FORMAT_XRGB8888;
	attributes[n++] = EGL_DMA_BUF_PLANE0_FD_EXT;
	attributes[n++] = fd;
	attributes[n++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
	attributes[n++] = 0;
	attributes[n++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
	attributes[n++] = (EGLint)stride;
	if (useModifier) {
		attributes[n++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
		attributes[n++] = (EGLint)(modifier & 0xffffffff);
		attributes[n++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
		attributes[n++] = (EGLint)(modifier >> 32);
	}
	attributes[n++] = EGL_NONE;
	return sCreateImage(display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT,
		NULL, attributes);
}


#ifndef DRM_FORMAT_MOD_INVALID
#	define DRM_FORMAT_MOD_INVALID 0x00ffffffffffffffULL
#endif


static bool
has_modifier(uint64 modifier)
{
	return modifier != DRM_FORMAT_MOD_INVALID;
}

#endif	// HAVE_GBM


SwapChainGpuCopy::SwapChainGpuCopy()
	:
	fDevice(NULL),
	fDisplay(NULL),
	fContext(NULL),
	fProgram(0),
	fBuffer(0),
	fFrameBuffer(0),
	fTexture(0),
	fTargetImage(NULL),
	fBo(NULL),
	fWidth(0),
	fHeight(0),
	fBits(NULL),
	fMapData(NULL),
	fStride(0),
	fTime(0),
	fFailed(false),
	fNativeFence(false)
{
}


SwapChainGpuCopy::~SwapChainGpuCopy()
{
#ifdef HAVE_GBM
	if (fContext != NULL
			&& eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
				fContext)) {
		_FreeTarget();
		if (fProgram != 0)
			glDeleteProgram(fProgram);
		if (fBuffer != 0)
			glDeleteBuffers(1, &fBuffer);
		for (size_t i = 0; i < fSources.size(); i++)
			_FreeSource(fSources[i]);
		fSources.clear();
		eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
			EGL_NO_CONTEXT);
	}
	if (fContext != NULL) {
		eglDestroyContext(fDisplay, fContext);
		// Drop this thread's EGL state so the driver can retire the context.
		eglReleaseThread();
	}
#endif
}


bool
SwapChainGpuCopy::_Fail(const char* why)
{
	if (!fFailed)
		fprintf(stderr, "swap chain gpu copy: %s; using the CPU path\n", why);
	fFailed = true;
#ifdef HAVE_GBM
	if (fContext != NULL)
		eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
			EGL_NO_CONTEXT);
#endif
	return false;
}


#ifdef HAVE_GBM

static GLuint
compile(GLenum type, const char* source)
{
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	GLint ok = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}


bool
SwapChainGpuCopy::_Setup(struct gbm_device* device)
{
	fDevice = device;
	sCreateImage = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress(
		"eglCreateImageKHR");
	sDestroyImage = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress(
		"eglDestroyImageKHR");
	sImageTarget = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress(
		"glEGLImageTargetTexture2DOES");
	sCreateSync = (PFNEGLCREATESYNCKHRPROC)eglGetProcAddress(
		"eglCreateSyncKHR");
	sClientWait = (PFNEGLCLIENTWAITSYNCKHRPROC)eglGetProcAddress(
		"eglClientWaitSyncKHR");
	sDestroySync = (PFNEGLDESTROYSYNCKHRPROC)eglGetProcAddress(
		"eglDestroySyncKHR");
	sDupFence = (PFNEGLDUPNATIVEFENCEFDANDROIDPROC)eglGetProcAddress(
		"eglDupNativeFenceFDANDROID");
	if (sCreateImage == NULL || sDestroyImage == NULL || sImageTarget == NULL)
		return _Fail("no EGL image support");

	PFNEGLGETPLATFORMDISPLAYEXTPROC getDisplay
		= (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress(
			"eglGetPlatformDisplayEXT");
	if (getDisplay == NULL)
		return _Fail("no EGL platform display");
	EGLDisplay display = getDisplay(EGL_PLATFORM_GBM_MESA, device, NULL);
	EGLint major, minor;
	if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor))
		return _Fail("EGL display failed");
	fDisplay = display;

	const char* extensions = eglQueryString(display, EGL_EXTENSIONS);
	if (extensions == NULL
			|| strstr(extensions, "EGL_KHR_surfaceless_context") == NULL
			|| strstr(extensions, "EGL_EXT_image_dma_buf_import") == NULL)
		return _Fail("missing EGL extensions");
	// Explicit release fences where the driver exports them.
	fNativeFence = sDupFence != NULL && sCreateSync != NULL
		&& strstr(extensions, "EGL_ANDROID_native_fence_sync") != NULL;

	if (!eglBindAPI(EGL_OPENGL_ES_API))
		return _Fail("no GLES");
	const EGLint config[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	fContext = eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT,
		config);
	if (fContext == EGL_NO_CONTEXT) {
		fContext = NULL;
		return _Fail("EGL context failed");
	}
	if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, fContext))
		return _Fail("EGL make current failed");

	const char* vertex =
		"attribute vec2 p;\n"
		"varying vec2 uv;\n"
		"void main() { uv = p * 0.5 + 0.5; gl_Position = vec4(p, 0.0, 1.0); }\n";
	const char* fragment =
		"precision mediump float;\n"
		"varying vec2 uv;\n"
		"uniform sampler2D t;\n"
		"void main() { gl_FragColor = vec4(texture2D(t, uv).rgb, 1.0); }\n";
	GLuint vs = compile(GL_VERTEX_SHADER, vertex);
	GLuint fs = compile(GL_FRAGMENT_SHADER, fragment);
	if (vs == 0 || fs == 0)
		return _Fail("shader compile failed");
	fProgram = glCreateProgram();
	glAttachShader(fProgram, vs);
	glAttachShader(fProgram, fs);
	glBindAttribLocation(fProgram, 0, "p");
	glLinkProgram(fProgram);
	glDeleteShader(vs);
	glDeleteShader(fs);
	GLint ok = 0;
	glGetProgramiv(fProgram, GL_LINK_STATUS, &ok);
	if (!ok)
		return _Fail("shader link failed");

	const GLfloat quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	glGenBuffers(1, &fBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, fBuffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
	return true;
}


void
SwapChainGpuCopy::Forget(const ServerBitmap* bitmap)
{
	BAutolock locker(fForgetLock);
	try {
		fForgotten.push_back(bitmap);
	} catch (...) {
	}
}


// Context current.
void
SwapChainGpuCopy::_FreeSource(Source& source)
{
	if (source.texture != 0)
		glDeleteTextures(1, &source.texture);
	if (source.image != NULL)
		sDestroyImage(fDisplay, source.image);
	if (source.fd >= 0)
		close(source.fd);
}


// Context current.
void
SwapChainGpuCopy::_Purge()
{
	std::vector<const ServerBitmap*> forgotten;
	{
		BAutolock locker(fForgetLock);
		forgotten.swap(fForgotten);
	}
	for (size_t i = 0; i < forgotten.size(); i++) {
		for (size_t j = 0; j < fSources.size(); j++) {
			if (fSources[j].bitmap != forgotten[i])
				continue;
			_FreeSource(fSources[j]);
			fSources.erase(fSources.begin() + j);
			break;
		}
	}
}


// Imports the buffer on first use; context current.
SwapChainGpuCopy::Source*
SwapChainGpuCopy::_Source(ServerBitmap* bitmap, int32 width, int32 height)
{
	static const size_t kMaxSources = 16;

	for (size_t i = 0; i < fSources.size(); i++) {
		if (fSources[i].bitmap == bitmap) {
			fSources[i].used = system_time();
			return &fSources[i];
		}
	}

	// Stale entries only outlive a buffer when a Forget was missed.
	if (fSources.size() >= kMaxSources) {
		size_t oldest = 0;
		for (size_t i = 1; i < fSources.size(); i++) {
			if (fSources[i].used < fSources[oldest].used)
				oldest = i;
		}
		_FreeSource(fSources[oldest]);
		fSources.erase(fSources.begin() + oldest);
	}

	struct gbm_bo* bo = bitmap->GbmBuffer()->BufferObject();
	Source source;
	source.bitmap = bitmap;
	source.image = NULL;
	source.texture = 0;
	source.fd = bitmap->GbmBuffer()->ExportFd();
	source.used = system_time();
	if (source.fd < 0) {
		_Fail("source export failed");
		return NULL;
	}
	buffer_info info;
	BufferAllocator::Describe(bo, &info);
	source.image = import_image(fDisplay, source.fd, width, height,
		info.stride, info.modifier, has_modifier(info.modifier));
	if (source.image == EGL_NO_IMAGE_KHR) {
		source.image = NULL;
		_FreeSource(source);
		_Fail("source import failed");
		return NULL;
	}
	glGenTextures(1, &source.texture);
	glBindTexture(GL_TEXTURE_2D, source.texture);
	sImageTarget(GL_TEXTURE_2D, source.image);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	if (glGetError() != GL_NO_ERROR) {
		_FreeSource(source);
		_Fail("source texture failed");
		return NULL;
	}
	try {
		fSources.push_back(source);
	} catch (...) {
		_FreeSource(source);
		_Fail("out of memory");
		return NULL;
	}
	return &fSources.back();
}


void
SwapChainGpuCopy::_FreeTarget()
{
	if (fBits != NULL && fBo != NULL)
		BufferAllocator::Unmap(fBo, fMapData);
	fBits = NULL;
	fMapData = NULL;
	if (fFrameBuffer != 0)
		glDeleteFramebuffers(1, &fFrameBuffer);
	if (fTexture != 0)
		glDeleteTextures(1, &fTexture);
	fFrameBuffer = fTexture = 0;
	if (fTargetImage != NULL)
		sDestroyImage(fDisplay, fTargetImage);
	fTargetImage = NULL;
	BufferAllocator::Destroy(fBo);
	fBo = NULL;
}


bool
SwapChainGpuCopy::_TryTarget(int32 width, int32 height,
	const uint64* modifiers, int32 count)
{
	BufferAllocator* allocator = BufferAllocator::For(fDevice);
	if (allocator == NULL)
		return false;

	// Read back through the detiling map, so any layout will do.
	fBo = allocator->Allocate(width, height, GBM_FORMAT_XRGB8888, modifiers,
		count, kBufferUsageRender);
	if (fBo == NULL)
		return false;

	const int fd = BufferAllocator::Export(fBo);
	if (fd < 0)
		return false;
	buffer_info info;
	BufferAllocator::Describe(fBo, &info);
	fTargetImage = import_image(fDisplay, fd, width, height, info.stride,
		info.modifier, has_modifier(info.modifier));
	close(fd);
	if (fTargetImage == EGL_NO_IMAGE_KHR) {
		fTargetImage = NULL;
		return false;
	}

	glGenTextures(1, &fTexture);
	glBindTexture(GL_TEXTURE_2D, fTexture);
	sImageTarget(GL_TEXTURE_2D, fTargetImage);
	glGenFramebuffers(1, &fFrameBuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, fFrameBuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_TEXTURE_2D, fTexture, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE
			|| glGetError() != GL_NO_ERROR)
		return false;
	return true;
}


bool
SwapChainGpuCopy::_Target(int32 width, int32 height)
{
	if (fBo != NULL && fWidth == width && fHeight == height)
		return true;

	_FreeTarget();

	// The renderer's own list first when it has no linear (it cannot
	// render to one then), linear otherwise; the other as the fallback.
	uint64 render[kBufferMaxModifiers];
	const int32 count = BufferAllocator::For(fDevice) != NULL
		? BufferAllocator::For(fDevice)->RenderModifiers(GBM_FORMAT_XRGB8888,
			render, kBufferMaxModifiers) : 0;
	bool hasLinear = count == 0;
	for (int32 i = 0; i < count; i++)
		hasLinear |= render[i] == kBufferModifierLinear;

	bool done = false;
	if (!hasLinear)
		done = _TryTarget(width, height, render, count);
	if (!done) {
		_FreeTarget();
		done = _TryTarget(width, height, NULL, 0);
	}
	if (!done && hasLinear && count > 0) {
		_FreeTarget();
		done = _TryTarget(width, height, render, count);
	}
	if (!done)
		return _Fail("target buffer failed");

	fWidth = width;
	fHeight = height;
	return true;
}


bool
SwapChainGpuCopy::Copy(struct gbm_device* device, ServerBitmap* bitmap,
	const BRect* area)
{
	if (fBits != NULL && fBo != NULL)
		BufferAllocator::Unmap(fBo, fMapData);
	fBits = NULL;
	fMapData = NULL;
	if (fFailed)
		return false;
	if (device == NULL || bitmap == NULL || bitmap->GbmBuffer() == NULL
			|| bitmap->GbmBuffer()->BufferObject() == NULL)
		return false;
	if (fDevice == NULL) {
		if (!_Setup(device))
			return false;
	} else if (fDevice != device) {
		return _Fail("device changed");
	}

	const bigtime_t start = system_time();
	if (!eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, fContext))
		return _Fail("EGL make current failed");

	const int32 width = (int32)bitmap->Bounds().Width() + 1;
	const int32 height = (int32)bitmap->Bounds().Height() + 1;
	if (!_Target(width, height))
		return false;

	_Purge();
	Source* source = _Source(bitmap, width, height);
	if (source == NULL)
		return false;
	const int fd = source->fd;

	glBindFramebuffer(GL_FRAMEBUFFER, fFrameBuffer);
	glViewport(0, 0, width, height);
	glDisable(GL_BLEND);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, source->texture);
	glUseProgram(fProgram);
	glBindBuffer(GL_ARRAY_BUFFER, fBuffer);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	bool done = false;
	if (fNativeFence) {
		// The fence says when the GPU is done reading the source: wait on
		// it here and hand it to the dma-buf for whoever writes it next.
		EGLSyncKHR sync = sCreateSync(fDisplay, EGL_SYNC_NATIVE_FENCE_ANDROID,
			NULL);
		if (sync != EGL_NO_SYNC_KHR) {
			glFlush();
			const int fence = sDupFence(fDisplay, sync);
			sDestroySync(fDisplay, sync);
			if (fence >= 0) {
				struct pollfd wait = { fence, POLLIN, 0 };
				done = poll(&wait, 1, 100) > 0;
#ifdef DMA_BUF_IOCTL_IMPORT_SYNC_FILE
				struct dma_buf_import_sync_file release;
				release.flags = DMA_BUF_SYNC_READ;
				release.fd = fence;
				ioctl(fd, DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &release);
#endif
				close(fence);
			}
		}
	}
	if (!done && sCreateSync != NULL && sClientWait != NULL
			&& sDestroySync != NULL) {
		EGLSyncKHR sync = sCreateSync(fDisplay, EGL_SYNC_FENCE_KHR, NULL);
		if (sync != EGL_NO_SYNC_KHR) {
			glFlush();
			done = sClientWait(fDisplay, sync,
				EGL_SYNC_FLUSH_COMMANDS_BIT_KHR, 100000000LL)
					== EGL_CONDITION_SATISFIED_KHR;
			sDestroySync(fDisplay, sync);
		}
	}
	if (!done)
		glFinish();
	const GLenum error = glGetError();
	glBindTexture(GL_TEXTURE_2D, 0);
	if (error != GL_NO_ERROR)
		return _Fail("GL error");

	uint32 stride = 0;
	uint32 left = 0;
	uint32 top = 0;
	uint32 mapWidth = width;
	uint32 mapHeight = height;
	if (area != NULL && area->IsValid() && area->left >= 0 && area->top >= 0
			&& area->right < width && area->bottom < height) {
		left = (uint32)area->left;
		top = (uint32)area->top;
		mapWidth = (uint32)area->right - left + 1;
		mapHeight = (uint32)area->bottom - top + 1;
	}
	void* map = BufferAllocator::Map(fBo, left, top, mapWidth, mapHeight,
		false, &stride, &fMapData);
	if (map == NULL)
		return _Fail("target map failed");
	fBits = (const uint8*)((uintptr_t)map - (size_t)top * stride
		- (size_t)left * 4);
	fStride = stride;
	fTime = system_time() - start;
	eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
		EGL_NO_CONTEXT);
	return true;
}

#else	// !HAVE_GBM

void SwapChainGpuCopy::Forget(const ServerBitmap*) {}
bool SwapChainGpuCopy::_Setup(struct gbm_device*) { return false; }
bool SwapChainGpuCopy::_Target(int32, int32) { return false; }
void SwapChainGpuCopy::_FreeTarget() {}
bool SwapChainGpuCopy::Copy(struct gbm_device*, ServerBitmap*, const BRect*)
{
	return false;
}

#endif
