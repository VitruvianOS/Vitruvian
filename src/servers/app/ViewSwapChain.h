/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef VIEW_SWAP_CHAIN_H
#define VIEW_SWAP_CHAIN_H


#include <vector>

#include <Locker.h>
#include <Referenceable.h>
#include <Region.h>

#include <SwapChain.h>


class DrawingEngine;
class ServerApp;
class ServerBitmap;
class ServerWindow;
class View;

namespace BPrivate {
	class PortLink;
}


// One presented buffer on its way to the screen.
struct SwapChainFrame {
	ServerBitmap*		bitmap;
	int32				token;
	int32				index;
	// Screen rect of the buffer, whole pixels.
	BRect				destination;
	// Empty means the whole buffer.
	const BRegion*		damage;
	DrawingEngine*		engine;
	// The caller holds the engine's parallel lock already.
	bool				engineLocked;
	// Drawing clipping of the view.
	const BRegion*		clipping;
	uint32				flags;
};


// What shows a presented buffer: a hardware plane or the CPU painter.
class SwapChainConsumer {
public:
	virtual						~SwapChainConsumer() {}

	// False when the buffer cannot be shown this way.
	virtual	bool				Show(const SwapChainFrame& frame) = 0;
	virtual	void				Hide() = 0;
};


// Server side of a view's swap chain: buffers the client renders into and
// the consumer that puts them on screen.
class ViewSwapChain {
public:
								ViewSwapChain(ServerApp* app,
									ServerWindow* window, View* view);
								~ViewSwapChain();

			status_t			Allocate(int32 width, int32 height,
									int32 count, bool resize);
			void				Describe(BPrivate::PortLink& sender) const;
			bool				Accepts(int32 token) const;
			uint32				Format() const;

			// Window and drawing engine locked as for drawing.
			void				Present(int32 token, int32 index,
									const BRegion& damage, uint32 flags,
									DrawingEngine* engine,
									const BRegion* clipping);
			// The view cannot show anything: free the buffer again.
			void				Drop(int32 token, int32 index);
			// The shown buffer under an exposed region.
			void				Redraw(DrawingEngine* engine,
									const BRegion& region);
			void				Hide();
			// Sends the queued release events; call outside any lock.
			void				SendReleases();

private:
			struct Generation;
			struct Release;

			Generation*			_Find(int32 token) const;
			void				_Retire(Generation* generation, int32 index,
									bigtime_t when);
			void				_Prune();
			BRect				_Destination(const Generation* generation)
									const;
			bool				_Visible() const;

			ServerApp*			fApp;
			ServerWindow*		fWindow;
			View*				fView;
			mutable BLocker		fLock;
			std::vector<Generation*> fGenerations;
			Generation*			fCurrent;
			// Buffer the consumer shows now.
			Generation*			fShownGeneration;
			int32				fShownIndex;
			SwapChainConsumer*	fConsumer;
			std::vector<Release> fReleases;
			bool				fCounted;
};


#endif	// VIEW_SWAP_CHAIN_H
