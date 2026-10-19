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

			// Keeps the old frame's overlap; the rest takes the fill colour
			// until the next frame arrives.
			void				Resize(int newWidth, int newHeight);
			// Runs the frame clock that completes WebKit's frame callbacks.
			void				ScheduleFrameComplete();
			void				SetFillColor(uint32_t bgra);

			BBitmap*			AcquireFront();
			void				ReleaseFront();

	struct	wpe_view_backend*	Backend() const { return fBackend; }
	// Hands the fdo exportable to WebKit, which destroys it with the view.
	struct	wpe_view_backend_exportable_fdo*	ReleaseExportable();

			std::function<void()> onFrameReady;

private:
			void				_AllocateBitmaps(const BBitmap* old = nullptr);
			void				_BlitSHMFrame(const uint8_t* data, int stride,
									int srcW, int srcH);
			void				_SwapBuffersLocked();

			struct wpe_view_backend_exportable_fdo* fExportable;
			bool				fOwnsExportable;
	struct	wpe_view_backend*	fBackend;

			BBitmap*			frontBitmap;
			BBitmap*			fBackBitmap;

			int					fWidth;
			int					fHeight;
			uint32_t			fFillColor;

			std::mutex				fBitmapMutex;
			std::atomic<bool>		fFrameReady;
			std::atomic<bool>		fClockRunning;
			std::atomic<int64_t>	fLastActivity;
			unsigned int			fClockSource;

	static	int					_ClockTick(void* data);
};


#endif // VOS_COMPOSITOR_ADAPTER_H
