/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef SWAP_CHAIN_GPU_COPY_H
#define SWAP_CHAIN_GPU_COPY_H


#include <atomic>
#include <mutex>
#include <vector>

#include <Locker.h>
#include <Rect.h>
#include <SupportDefs.h>

class BRegion;
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
	// Drops what is cached for a buffer that is going away; the GL
	// objects go at the next Copy or with this object. Any thread.
			void				Forget(const ServerBitmap* bitmap);
	// The copy, valid until the next Copy; addressed from the buffer
	// origin, but only the area is readable.
			const uint8*		Bits() const { return fBits; }
			uint32				Stride() const { return fStride; }
			bigtime_t			Time() const { return fTime; }
			bool				Failed() const { return fFailed; }

private:
			struct Source {
				const ServerBitmap*	bitmap;
				void*				image;
				uint32				texture;
				int					fd;
				bigtime_t			used;
			};

			Source*				_Source(ServerBitmap* bitmap, int32 width,
									int32 height);
			void				_FreeSource(Source& source);
			void				_Purge();
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
			std::vector<Source>	fSources;
			std::vector<const ServerBitmap*> fForgotten;
			BLocker				fForgetLock;
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


// Draws the desktop and client buffers into scanout buffers on the
// display GPU, so no buffer is read back. The desktop stays a texture
// that only takes the damaged rects. One context, used by whoever holds
// the lock; any thread.
class SwapChainCompose {
public:
	enum {
		kOk,
		kFailed
	};

			// A source drawn over the desktop, only where clip lets it.
			struct Layer {
				int32			source;
				BRect			destination;
				const BRegion*	clip;
			};

								SwapChainCompose(struct gbm_device* device);
								~SwapChainCompose();

			// A latched failure: the GPU path is off for good.
			bool				Failed() const { return fFailed; }

			// Imports a client dma-buf; the id, or -1 when the GPU cannot
			// use it (another GPU's tiling, say).
			int32				AddSource(int fd, int32 width, int32 height,
									uint32 stride, uint64 modifier);
			void				RemoveSource(int32 id);

			// Makes the scanout buffers; the modifiers are what the plane
			// takes. The framebuffer of one of them is returned, to try.
			bool				Prepare(int32 width, int32 height,
									const uint64* modifiers, int32 count,
									uint32* _fb);
			// The desktop texture no longer matches the CPU buffer.
			void				Invalidate();
			// Renders a frame into the buffer that is not on screen.
			// bits is the desktop, damage what changed since the last
			// call (all when null or after Invalidate); the layers go over
			// it in order, one whose source was removed meanwhile is skipped.
			int					Render(int32 width, int32 height,
									const uint8* bits, uint32 stride,
									const BRegion* damage,
									const Layer* layers, int32 count,
									uint32* _fb);
			// The frame last rendered is the one going on screen.
			void				Shown();
			// The buffers go once the desktop is on screen again.
			void				ReleaseTargets();

private:
			struct Texture {
				int32			id;
				void*			image;
				uint32			texture;
			};
			struct Target {
				struct gbm_bo*	bo;
				void*			image;
				uint32			texture;
				uint32			frameBuffer;
				uint32			fb;
			};
			static const int32	kTargets = 2;

			bool				_Setup();
			bool				_Make();
			void				_Release();
			bool				_Fail(const char* why);
			bool				_Targets(int32 width, int32 height);
			bool				_MakeTarget(Target& target, int32 width,
									int32 height, const uint64* modifiers,
									int32 count);
			void				_FreeTargets();
			bool				_Desktop(int32 width, int32 height);
			void				_Upload(const uint8* bits, uint32 stride,
									const BRect& area);
			void				_Draw(uint32 texture, const BRect& area,
									bool swizzle, int32 width, int32 height);
			Texture*			_Find(int32 id);

			std::mutex			fLock;
			struct gbm_device*	fDevice;
			void*				fDisplay;
			void*				fContext;
			uint32				fProgram;
			uint32				fBuffer;
			int32				fRectLocation;
			int32				fSwizzleLocation;
			uint32				fDesktop;
			int32				fDesktopWidth;
			int32				fDesktopHeight;
			bool				fDesktopValid;
			std::vector<uint8>	fScratch;
			std::vector<Texture> fSources;
			int32				fNextSource;
			Target				fTargets[kTargets];
			int32				fTargetCount;
			int32				fWidth;
			int32				fHeight;
			int32				fLast;
			int32				fOnScreen;
			uint64				fModifiers[64];
			int32				fModifierCount;
			bool				fRefusedWarned;
			std::atomic<bool>	fFailed;
};


#endif	// SWAP_CHAIN_GPU_COPY_H
