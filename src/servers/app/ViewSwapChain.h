/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef VIEW_SWAP_CHAIN_H
#define VIEW_SWAP_CHAIN_H


#include <condition_variable>
#include <mutex>
#include <vector>

#include <Locker.h>
#include <Referenceable.h>
#include <Region.h>

#include <SwapChain.h>


class DrawingEngine;
class HWInterface;
class PlaneConsumer;
class ServerApp;
class ServerBitmap;
class ServerWindow;
class View;
class Window;

namespace BPrivate {
	class PortLink;
}


// Waits (bounded) for the GPU to finish writing a chain buffer and brackets
// CPU reads of it. Does nothing without GBM.
class SwapChainCpuRead {
public:
								SwapChainCpuRead(ServerBitmap* bitmap);
								~SwapChainCpuRead();

private:
			int					fFd;
};


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

	// Plane consumer: every buffer of the chain, the output the planes
	// belong to and whether the whole destination can be seen.
	::HWInterface*		hardware;
	ServerBitmap*		buffers[BPrivate::kSwapChainMaxBuffers];
	int32				count;
	BRect				outputFrame;
	bool				fullyVisible;
	// Names the update in the plane callbacks.
	uint64				serial;
	// Set by a consumer that reports completion later.
	bool				async;
	// Set by the CPU consumer: microseconds spent copying.
	bigtime_t			copyTime;

	SwapChainFrame()
		:
		bitmap(NULL),
		token(0),
		index(0),
		damage(NULL),
		engine(NULL),
		engineLocked(false),
		clipping(NULL),
		flags(0),
		hardware(NULL),
		count(0),
		fullyVisible(false),
		serial(0),
		async(false),
		copyTime(0)
	{
		for (int32 i = 0; i < BPrivate::kSwapChainMaxBuffers; i++)
			buffers[i] = NULL;
	}
};


// What shows a presented buffer: a hardware plane or the CPU painter.
class SwapChainConsumer {
public:
	virtual						~SwapChainConsumer() {}

	// False when the buffer cannot be shown this way.
	virtual	bool				Show(SwapChainFrame& frame) = 0;
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

			// Window and drawing engine locked as for drawing. A NULL
			// clipping says the view's clipping is unchanged since the
			// last present.
			void				Present(int32 token, int32 index,
									const BRegion& damage, uint32 flags,
									DrawingEngine* engine,
									const BRegion* clipping);
			// The view cannot show anything: free the buffer again.
			void				Drop(int32 token, int32 index);
			// The shown buffer under an exposed region.
			void				Redraw(DrawingEngine* engine,
									const BRegion& region);
			// Takes the buffer off its plane; the painter shows it again.
			void				Hide(bool redraw, const char* reason);
			// Copies the buffer on a plane into a screen read. Desktop
			// window lock held.
			void				Compose(ServerBitmap* target,
									const BRect& bounds);
			// The window changed under a plane.
			void				Revalidate();
			void				Detach();
			// Shows the frame the CPU path holds back; once per refresh.
			void				Flush(DrawingEngine* engine);

			// From the plane consumer.
			void				NotifyShown(uint64 serial, bigtime_t when);
			void				NotifyDropped(uint64 serial);
			void				NotifyLost(const BRect& area);
			// Staged updates older than serial will not be shown.
			void				NotifyReleased(uint64 serial);

private:
			struct Generation;
			struct Pending;

			Generation*			_Find(int32 token) const;
			void				_Retire(Generation* generation, int32 index,
									bigtime_t when);
			void				_DropPending(uint64 below);
			void				_Prune();
			BRect				_DestinationFor(int32 width,
									int32 height) const;
			BRect				_Destination(const Generation* generation)
									const;
			bool				_Post(const SwapChainFrame& frame,
									Generation* generation);
			void				_DropMailbox();
			void				_ResetMailbox();
	static	int32				_PumpEntry(void* self);
			void				_Pump();
			void				_UpdatePeriod(::HWInterface* hardware);
			void				_CountShown(bigtime_t when);
			void				_CountReplaced();
			void				_CountCopy(bigtime_t time);
			void				_Report(bigtime_t now);

			ServerApp*			fApp;
			ServerWindow*		fWindow;
			View*				fView;
			::Window*			fOwnerWindow;
			mutable BLocker		fLock;
			std::vector<Generation*> fGenerations;
			Generation*			fCurrent;
			// Buffer on screen now.
			Generation*			fShownGeneration;
			int32				fShownIndex;
			SwapChainConsumer*	fCpu;
			PlaneConsumer*		fPlane;
			std::vector<Pending> fPending;
			uint64				fSerial;
			int32				fPeriod;
			bigtime_t			fPeriodAt;
			bool				fCounted;

			// Frame timing, reported with VOS_SWAP_CHAIN_STATS=1.
			bool				fStats;
			bigtime_t			fStatsStart;
			bigtime_t			fLastShown;
			bigtime_t			fWorstGap;
			uint32				fPresented;
			uint32				fShown;
			uint32				fReplaced;
			int64				fCopyTime;
			int32				fCopies;

			// CPU path: the newest frame, shown by the pump.
			thread_id			fPump;
			std::mutex			fPumpMutex;
			std::condition_variable	fPumpCondition;
			bool				fPumpWake;
			bool				fPumpQuit;
			uint64				fMailSerial;
			Generation*			fMailGeneration;
			bool				fMailWhole;
			BRegion				fMailDamage;
};


#endif	// VIEW_SWAP_CHAIN_H
