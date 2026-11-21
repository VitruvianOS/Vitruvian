/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _BUFFER_ALLOCATOR_H
#define _BUFFER_ALLOCATOR_H


#include <pthread.h>
#include <SupportDefs.h>


struct gbm_bo;
struct gbm_device;


namespace BPrivate {

// How a buffer is going to be used; they combine.
enum {
	kBufferUsageScanout		= 0x1,
	kBufferUsageRender		= 0x2,
	// The CPU maps it as plain memory: always linear.
	kBufferUsageCpu			= 0x4
};

const uint64 kBufferModifierLinear = 0;
const uint64 kBufferModifierInvalid = 0x00ffffffffffffffULL;

enum {
	kBufferMaxModifiers		= 64
};

struct buffer_info {
	uint32	width;
	uint32	height;
	uint32	fourcc;
	uint64	modifier;
	uint32	stride;
	uint32	offset;
};


/*!	The one place buffers are allocated for the compositor and its clients.
	GBM underneath, so Mesa and the proprietary stacks go through the same
	calls. One instance per GBM device.
*/
class BufferAllocator {
public:
	static	BufferAllocator*	For(struct gbm_device* device);

			// The modifiers the GL renderer of this device can import and
			// render to for the format; empty when it cannot tell.
			int32				RenderModifiers(uint32 fourcc, uint64* modifiers,
									int32 max);

			// Modifiers in both lists, in the order of the first; a list
			// that is empty or holds the invalid modifier puts no limit.
			// Linear is only kept when everyone takes it.
	static	int32				Intersect(const uint64* first, int32 firstCount,
									const uint64* second, int32 secondCount,
									uint64* result, int32 max);

			// Without modifiers, or for CPU usage, the buffer is linear.
			// Falls back to a linear buffer when the driver refuses the
			// list or has no modifier support.
			struct gbm_bo*		Allocate(uint32 width, uint32 height,
									uint32 fourcc, const uint64* modifiers,
									int32 modifierCount, uint32 usage);
			struct gbm_bo*		Import(int fd, uint32 width, uint32 height,
									uint32 fourcc, uint32 stride,
									uint32 offset, uint64 modifier,
									uint32 usage);

	static	void				Describe(struct gbm_bo* bo, buffer_info* info);
			// dma-buf; the caller closes it.
	static	int					Export(struct gbm_bo* bo);
	static	bool				IsLinear(struct gbm_bo* bo);

			// Detiled, CPU readable view of a rectangle.
	static	void*				Map(struct gbm_bo* bo, uint32 x, uint32 y,
									uint32 width, uint32 height, bool write,
									uint32* stride, void** cookie);
	static	void				Unmap(struct gbm_bo* bo, void* cookie);
	static	void				Destroy(struct gbm_bo* bo);

private:
								BufferAllocator(struct gbm_device* device);

	static	pthread_mutex_t*	_LockFor(struct gbm_bo* bo);

			// Mesa keeps one internal context per device, not thread-safe:
			// every call into gbm on this device holds it.
			pthread_mutex_t		fLock;
			struct gbm_device*	fDevice;
			void*				fDisplay;
			bool				fDisplayTried;
};

}	// namespace BPrivate


#endif	// _BUFFER_ALLOCATOR_H
