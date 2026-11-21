/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include <BufferAllocator.h>

#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include <new>

#include <vector>

#ifdef HAVE_GBM
#	include <gbm.h>
#	include <EGL/egl.h>
#	include <EGL/eglext.h>
#endif


namespace BPrivate {

#ifdef HAVE_GBM

namespace {

struct Entry {
	struct gbm_device*	device;
	BufferAllocator*	allocator;
};

pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
std::vector<Entry>* sAllocators = NULL;


namespace {

struct DeviceLocker {
	DeviceLocker(pthread_mutex_t* lock)
		:
		fLock(lock)
	{
		pthread_mutex_lock(fLock);
	}

	~DeviceLocker()
	{
		pthread_mutex_unlock(fLock);
	}

	pthread_mutex_t*	fLock;
};

}	// namespace

}	// namespace


BufferAllocator::BufferAllocator(struct gbm_device* device)
	:
	fDevice(device),
	fDisplay(NULL),
	fDisplayTried(false)
{
	pthread_mutexattr_t attributes;
	pthread_mutexattr_init(&attributes);
	pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&fLock, &attributes);
	pthread_mutexattr_destroy(&attributes);
}


BufferAllocator*
BufferAllocator::For(struct gbm_device* device)
{
	if (device == NULL)
		return NULL;

	pthread_mutex_lock(&sLock);
	if (sAllocators == NULL)
		sAllocators = new(std::nothrow) std::vector<Entry>;
	BufferAllocator* found = NULL;
	if (sAllocators != NULL) {
		for (size_t i = 0; i < sAllocators->size(); i++) {
			if ((*sAllocators)[i].device == device) {
				found = (*sAllocators)[i].allocator;
				break;
			}
		}
		if (found == NULL) {
			found = new(std::nothrow) BufferAllocator(device);
			if (found != NULL) {
				Entry entry = { device, found };
				try {
					sAllocators->push_back(entry);
				} catch (std::bad_alloc&) {
					delete found;
					found = NULL;
				}
			}
		}
	}
	pthread_mutex_unlock(&sLock);
	return found;
}


int32
BufferAllocator::RenderModifiers(uint32 fourcc, uint64* modifiers, int32 max)
{
	pthread_mutex_lock(&sLock);
	if (!fDisplayTried) {
		fDisplayTried = true;
		PFNEGLGETPLATFORMDISPLAYEXTPROC getDisplay
			= (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress(
				"eglGetPlatformDisplayEXT");
		EGLint major, minor;
		if (getDisplay != NULL) {
			EGLDisplay display = getDisplay(EGL_PLATFORM_GBM_MESA, fDevice,
				NULL);
			if (display != EGL_NO_DISPLAY
					&& eglInitialize(display, &major, &minor))
				fDisplay = display;
		}
	}
	EGLDisplay display = (EGLDisplay)fDisplay;
	pthread_mutex_unlock(&sLock);
	if (display == NULL)
		return 0;

	const char* extensions = eglQueryString(display, EGL_EXTENSIONS);
	if (extensions == NULL
			|| strstr(extensions, "EGL_EXT_image_dma_buf_import_modifiers")
				== NULL)
		return 0;

	PFNEGLQUERYDMABUFMODIFIERSEXTPROC query
		= (PFNEGLQUERYDMABUFMODIFIERSEXTPROC)eglGetProcAddress(
			"eglQueryDmaBufModifiersEXT");
	if (query == NULL)
		return 0;

	EGLuint64KHR all[kBufferMaxModifiers];
	EGLBoolean external[kBufferMaxModifiers];
	EGLint count = 0;
	if (!query(display, (EGLint)fourcc, kBufferMaxModifiers, all, external,
			&count) || count <= 0)
		return 0;

	// A texture bound as external only cannot be a render target.
	int32 n = 0;
	for (EGLint i = 0; i < count && n < max; i++) {
		if (external[i] || all[i] == kBufferModifierInvalid)
			continue;
		modifiers[n++] = all[i];
	}
	return n;
}


int32
BufferAllocator::Intersect(const uint64* first, int32 firstCount,
	const uint64* second, int32 secondCount, uint64* result, int32 max)
{
	bool anyFirst = firstCount <= 0;
	bool anySecond = secondCount <= 0;
	for (int32 i = 0; i < firstCount; i++)
		anyFirst |= first[i] == kBufferModifierInvalid;
	for (int32 i = 0; i < secondCount; i++)
		anySecond |= second[i] == kBufferModifierInvalid;

	int32 n = 0;
	if (anyFirst) {
		for (int32 i = 0; i < secondCount && n < max; i++) {
			if (second[i] != kBufferModifierInvalid)
				result[n++] = second[i];
		}
		return n;
	}
	for (int32 i = 0; i < firstCount && n < max; i++) {
		if (first[i] == kBufferModifierInvalid)
			continue;
		bool found = anySecond;
		for (int32 j = 0; j < secondCount && !found; j++)
			found = second[j] == first[i];
		if (found)
			result[n++] = first[i];
	}
	return n;
}


static uint32
legacy_flags(uint32 usage, bool scanout)
{
	// No WRITE: Mesa takes it as a dumb cursor buffer request.
	uint32 flags = GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR;
	if (scanout)
		flags |= GBM_BO_USE_SCANOUT;
	(void)usage;
	return flags;
}


struct gbm_bo*
BufferAllocator::Allocate(uint32 width, uint32 height, uint32 fourcc,
	const uint64* modifiers, int32 modifierCount, uint32 usage)
{
	DeviceLocker locker(&fLock);
	if (fDevice == NULL || width == 0 || height == 0)
		return NULL;

	const bool scanout = (usage & kBufferUsageScanout) != 0;
	struct gbm_bo* bo = NULL;

	// A tiled buffer only when nobody maps it as plain memory.
	bool tiled = false;
	for (int32 i = 0; i < modifierCount && modifiers != NULL; i++)
		tiled |= modifiers[i] != kBufferModifierLinear;
	if (tiled && (usage & kBufferUsageCpu) == 0) {
		uint32 flags = GBM_BO_USE_RENDERING;
		if (scanout)
			flags |= GBM_BO_USE_SCANOUT;
		bo = gbm_bo_create_with_modifiers2(fDevice, width, height, fourcc,
			modifiers, (unsigned int)modifierCount, flags);
		if (bo == NULL && scanout) {
			bo = gbm_bo_create_with_modifiers2(fDevice, width, height,
				fourcc, modifiers, (unsigned int)modifierCount,
				GBM_BO_USE_RENDERING);
		}
		if (bo != NULL)
			return bo;
	}

	// Linear, the way it always was.
	if (scanout) {
		bo = gbm_bo_create(fDevice, width, height, fourcc,
			legacy_flags(usage, true));
	}
	if (bo == NULL) {
		bo = gbm_bo_create(fDevice, width, height, fourcc,
			legacy_flags(usage, false));
	}
	return bo;
}


struct gbm_bo*
BufferAllocator::Import(int fd, uint32 width, uint32 height, uint32 fourcc,
	uint32 stride, uint32 offset, uint64 modifier, uint32 usage)
{
	DeviceLocker locker(&fLock);
	if (fDevice == NULL || fd < 0)
		return NULL;

	uint32 flags = GBM_BO_USE_RENDERING;
	if ((usage & kBufferUsageScanout) != 0)
		flags |= GBM_BO_USE_SCANOUT;

	struct gbm_import_fd_modifier_data data;
	memset(&data, 0, sizeof(data));
	data.width = width;
	data.height = height;
	data.format = fourcc;
	data.num_fds = 1;
	data.fds[0] = fd;
	data.strides[0] = (int)stride;
	data.offsets[0] = (int)offset;
	data.modifier = modifier;
	return (struct gbm_bo*)gbm_bo_import(fDevice, GBM_BO_IMPORT_FD_MODIFIER,
		&data, flags);
}


pthread_mutex_t*
BufferAllocator::_LockFor(struct gbm_bo* bo)
{
	// A bo on a device without an allocator shares one fallback lock.
	static pthread_mutex_t sNoDeviceLock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
	BufferAllocator* allocator = bo != NULL
		? For(gbm_bo_get_device(bo)) : NULL;
	return allocator != NULL ? &allocator->fLock : &sNoDeviceLock;
}


void
BufferAllocator::Describe(struct gbm_bo* bo, buffer_info* info)
{
	DeviceLocker locker(_LockFor(bo));
	info->width = gbm_bo_get_width(bo);
	info->height = gbm_bo_get_height(bo);
	info->fourcc = gbm_bo_get_format(bo);
	info->modifier = gbm_bo_get_modifier(bo);
	info->stride = gbm_bo_get_stride(bo);
	info->offset = gbm_bo_get_offset(bo, 0);
}


int
BufferAllocator::Export(struct gbm_bo* bo)
{
	DeviceLocker locker(_LockFor(bo));
	return bo != NULL ? gbm_bo_get_fd(bo) : -1;
}


bool
BufferAllocator::IsLinear(struct gbm_bo* bo)
{
	DeviceLocker locker(_LockFor(bo));
	const uint64 modifier = gbm_bo_get_modifier(bo);
	return modifier == kBufferModifierLinear
		|| modifier == kBufferModifierInvalid;
}


void*
BufferAllocator::Map(struct gbm_bo* bo, uint32 x, uint32 y, uint32 width,
	uint32 height, bool write, uint32* stride, void** cookie)
{
	DeviceLocker locker(_LockFor(bo));
	return gbm_bo_map(bo, x, y, width, height,
		write ? (GBM_BO_TRANSFER_READ | GBM_BO_TRANSFER_WRITE)
			: GBM_BO_TRANSFER_READ, stride, cookie);
}


void
BufferAllocator::Unmap(struct gbm_bo* bo, void* cookie)
{
	DeviceLocker locker(_LockFor(bo));
	gbm_bo_unmap(bo, cookie);
}


void
BufferAllocator::Destroy(struct gbm_bo* bo)
{
	DeviceLocker locker(_LockFor(bo));
	if (bo != NULL)
		gbm_bo_destroy(bo);
}

#else	// !HAVE_GBM

BufferAllocator::BufferAllocator(struct gbm_device*) {}
BufferAllocator* BufferAllocator::For(struct gbm_device*) { return NULL; }
int32 BufferAllocator::RenderModifiers(uint32, uint64*, int32) { return 0; }
int32 BufferAllocator::Intersect(const uint64*, int32, const uint64*, int32,
	uint64*, int32) { return 0; }
struct gbm_bo* BufferAllocator::Allocate(uint32, uint32, uint32,
	const uint64*, int32, uint32) { return NULL; }
struct gbm_bo* BufferAllocator::Import(int, uint32, uint32, uint32, uint32,
	uint32, uint64, uint32) { return NULL; }
void BufferAllocator::Describe(struct gbm_bo*, buffer_info*) {}
int BufferAllocator::Export(struct gbm_bo*) { return -1; }
bool BufferAllocator::IsLinear(struct gbm_bo*) { return true; }
void* BufferAllocator::Map(struct gbm_bo*, uint32, uint32, uint32, uint32,
	bool, uint32*, void**) { return NULL; }
void BufferAllocator::Unmap(struct gbm_bo*, void*) {}
void BufferAllocator::Destroy(struct gbm_bo*) {}

#endif

}	// namespace BPrivate
