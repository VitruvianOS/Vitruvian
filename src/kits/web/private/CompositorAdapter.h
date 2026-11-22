/*
 * Copyright 2026, VitruvianOS Project.
 * Distributed under the terms of the LGPL License.
 */
#ifndef VOS_COMPOSITOR_ADAPTER_H
#define VOS_COMPOSITOR_ADAPTER_H


#include <Bitmap.h>
#ifndef HAS_WPE_PLATFORM
#include <wpe/wpe.h>
#include <wpe/fdo.h>
#endif

#include <algorithm>
#include <atomic>
#include <functional>
#include <mutex>


class DmabufPresenter;


struct CompositorAdapter {
								CompositorAdapter(int width, int height);
								~CompositorAdapter();

			// Keeps the old frame's overlap; the rest takes the fill colour
			// until the next frame arrives.
			void				Resize(int newWidth, int newHeight);
#ifndef HAS_WPE_PLATFORM
			// Runs the frame clock that completes WebKit's frame callbacks.
			void				ScheduleFrameComplete();
#endif
			void				SetFillColor(uint32_t bgra);

			// Copies a rendered SHM frame into the back bitmap and swaps.
			// The WPEPlatform view calls this from render_buffer; the FDO
			// export callback goes through it too.
			void				PresentSHMFrame(const uint8_t* data,
									int stride, int srcW, int srcH);

			// The window thread's swap chain for dma-buf frames; not owned.
			void				SetPresenter(DmabufPresenter* presenter)
									{ fPresenter = presenter; }
			DmabufPresenter*	Presenter() const { return fPresenter; }
			// The server shows the last frame itself; Draw adds nothing.
			void				SetDmabufActive(bool active)
									{ fDmabufActive.store(active); }
			bool				DmabufActive() const
									{ return fDmabufActive.load(); }

			BBitmap*			AcquireFront();
			void				ReleaseFront();
			// Window thread: the exported frame was drawn.
			void				NotifyPresented();

#ifndef HAS_WPE_PLATFORM
	struct	wpe_view_backend*	Backend() const { return fBackend; }
	// Hands the fdo exportable to WebKit, which destroys it with the view.
	struct	wpe_view_backend_exportable_fdo*	ReleaseExportable();
#endif

			std::function<void()> onFrameReady;

private:
			void				_AllocateBitmaps(const BBitmap* old = nullptr);
			void				_SwapBuffersLocked();

#ifndef HAS_WPE_PLATFORM
			void				_CompleteExported();
			void				_ArmFallback();
			struct wpe_view_backend_exportable_fdo* fExportable;
			bool				fOwnsExportable;
	struct	wpe_view_backend*	fBackend;
#endif

			BBitmap*			frontBitmap;
			BBitmap*			fBackBitmap;

			int					fWidth;
			int					fHeight;
			uint32_t			fFillColor;

			std::mutex				fBitmapMutex;
			std::atomic<bool>		fFrameReady;
			std::atomic<bool>		fDmabufActive;
			DmabufPresenter*		fPresenter;
#ifndef HAS_WPE_PLATFORM
			std::atomic<bool>		fClockRunning;
			std::atomic<int64_t>	fLastActivity;
			unsigned int			fClockSource;
			std::atomic<bool>		fOutstanding;
			unsigned int			fFallbackSource;

	static	int					_ClockTick(void* data);
	static	int					_FallbackTick(void* data);
#endif
};


#endif // VOS_COMPOSITOR_ADAPTER_H
