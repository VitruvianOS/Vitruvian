/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef SWAP_CHAIN_GPU_COPY_H
#define SWAP_CHAIN_GPU_COPY_H


#include <Rect.h>
#include <SupportDefs.h>

class ServerBitmap;
struct gbm_device;
struct gbm_bo;


// Copies a chain buffer into a cached linear bo on the GPU, so the CPU
// never reads the write-combined original.
class SwapChainGpuCopy {
public:
								SwapChainGpuCopy();
								~SwapChainGpuCopy();

	// 0 off, 1 forced, 2 chosen by measuring (VOS_SWAP_CHAIN_GPU_COPY).
	static	int					Mode();

	// Copies the buffer and waits for the GPU; only the area is mapped
	// for reading when given. False on any failure, after which the
	// object stays unusable.
			bool				Copy(struct gbm_device* device,
									ServerBitmap* bitmap,
									const BRect* area = NULL);
	// The copy, valid until the next Copy; addressed from the buffer
	// origin, but only the area is readable.
			const uint8*		Bits() const { return fBits; }
			uint32				Stride() const { return fStride; }
			bigtime_t			Time() const { return fTime; }
			bool				Failed() const { return fFailed; }

private:
			bool				_Setup(struct gbm_device* device);
			bool				_Target(int32 width, int32 height);
			bool				_TryTarget(int32 width, int32 height,
									const uint64* modifiers, int32 count);
			void				_FreeTarget();
			bool				_Fail(const char* why);

			struct gbm_device*	fDevice;
			void*				fDisplay;
			void*				fContext;
			uint32				fProgram;
			uint32				fBuffer;
			uint32				fFrameBuffer;
			uint32				fTexture;
			uint32				fSource;
			void*				fTargetImage;
			struct gbm_bo*		fBo;
			int32				fWidth;
			int32				fHeight;
			const uint8*		fBits;
			void*				fMapData;
			uint32				fStride;
			bigtime_t			fTime;
			bool				fFailed;
			bool				fNativeFence;
};


#endif	// SWAP_CHAIN_GPU_COPY_H
