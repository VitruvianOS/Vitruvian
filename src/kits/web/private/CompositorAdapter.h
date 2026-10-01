/*
 * Copyright 2026, VitruvianOS Project.
 * Distributed under the terms of the LGPL License.
 */
#ifndef VOS_COMPOSITOR_ADAPTER_H
#define VOS_COMPOSITOR_ADAPTER_H


#include <Bitmap.h>
#include <wpe/wpe.h>
#include <wpe/fdo.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <mutex>


struct CompositorAdapter {
								CompositorAdapter(int width, int height);
								~CompositorAdapter();

			void				Resize(int newWidth, int newHeight);

			BBitmap*			AcquireFront();
			void				ReleaseFront();

	struct	wpe_view_backend*	Backend() const { return fBackend; }

			std::function<void()> onFrameReady;

private:
			void				_AllocateBitmaps();
			void				_BlitSHMFrame(const uint8_t* data, int stride,
									int srcW, int srcH);
			void				_SwapBuffersLocked();

			struct wpe_view_backend_exportable_fdo* fExportable;
	struct	wpe_view_backend*	fBackend;

			BBitmap*			frontBitmap;
			BBitmap*			fBackBitmap;

			int					fWidth;
			int					fHeight;

			std::mutex				fBitmapMutex;
			std::atomic<bool>		fFrameReady;
};


#endif // VOS_COMPOSITOR_ADAPTER_H
