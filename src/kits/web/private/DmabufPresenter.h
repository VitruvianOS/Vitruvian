/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */
#ifndef VOS_DMABUF_PRESENTER_H
#define VOS_DMABUF_PRESENTER_H


#include <SwapChain.h>

#include <wpe/wpe-platform.h>

#include <functional>
#include <mutex>
#include <vector>


class BView;


// Shows WPE dma-bufs through the view's swap chain without a copy and
// tracks them until app_server shows something else. The window thread
// owns the chain (Attach/Resize/Detach); the GLib thread presents and
// polls. The presenter never touches the window lock on the GLib side.
class DmabufPresenter {
public:
	enum Result {
		kShown,
		// The caller falls back to copying (or drops the frame).
		kUnsupported,
		// The slot is still on screen; the frame is skipped.
		kBusy
	};

								DmabufPresenter();
								~DmabufPresenter();

	// Window thread, view attached.
			bool				Attach(BView* view, int width, int height);
			void				Resize(int width, int height);
			void				Detach();

			bool				IsReady() const;

	// GLib thread. The rects are the page damage, buffer coordinates.
			Result				Present(WPEBuffer* buffer, int fd,
									const BPrivate::swap_chain_external& description,
									const WPERectangle* damage,
									unsigned int damageCount);
	// Reports every buffer app_server is done with. False once nothing is
	// outstanding.
			bool				Poll();
	// Gives everything back; the chain stays usable.
			void				Flush();

	// GLib thread only. Rendered: the frame is on screen. Released: the
	// buffer may be reused.
			void				SetCallbacks(
									std::function<void(WPEBuffer*)> rendered,
									std::function<void(WPEBuffer*)> released);

private:
	struct Entry {
		WPEBuffer*	buffer;
		// -1 once the set it was imported into is gone.
		int			slot;
		bool		rendered;
	};

			void				_ForgetSlotsLocked();
			void				_Collect(std::vector<WPEBuffer*>& rendered,
									std::vector<WPEBuffer*>& released);

	mutable	std::mutex			fMutex;
			BPrivate::SwapChain*	fChain;
			WPEBuffer*			fSlots[BPrivate::kSwapChainMaxExternal];
			int					fSlotCount;
			std::vector<Entry>	fEntries;
			std::function<void(WPEBuffer*)>	fRendered;
			std::function<void(WPEBuffer*)>	fReleased;
};


#endif // VOS_DMABUF_PRESENTER_H
