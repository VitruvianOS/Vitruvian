/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SURFACE_H
#define _SURFACE_H


#include <Rect.h>
#include <Region.h>
#include <Window.h>


class BBitmap;
class BCursor;
class BMessage;

namespace BPrivate {

class PortLink;
class SwapChain;


// A top level window without a looper or a lock. The owner drives it from
// its own thread: events are read from EventPort() (port_to_fd() makes it
// pollable), content is rendered into Buffer() and shown with Present().
// Not thread safe; one thread per surface.
class BSurface {
public:
								BSurface(BRect frame, const char* title,
									window_look look, window_feel feel,
									uint32 flags,
									uint32 workspaces = B_CURRENT_WORKSPACE);
								~BSurface();

			status_t			InitCheck() const;

			port_id				EventPort() const;

			// Reads one event, waiting up to timeout. Exposure requests are
			// answered from Buffer() before returning; window moves and
			// resizes update Frame().
			status_t			NextEvent(BMessage* event,
									bigtime_t timeout = 0);

			void				MoveTo(BPoint where);
			void				ResizeTo(float width, float height);
			void				Show();
			void				Hide();
			void				SetTitle(const char* title);
			// Lists the window under another team (Deskbar, roster).
			void				SetTeam(team_id team);
			status_t			SetLook(window_look look);
			status_t			SetFeel(window_feel feel);
			status_t			SetFlags(uint32 flags);
			void				Activate(bool active = true);
			void				Minimize(bool minimize);
			// B_POINTER_EVENTS delivers pointer events outside the surface.
			void				SetEventMask(uint32 mask, uint32 options = 0);

			// Shown while the pointer is over the surface; NULL is ignored.
			void				SetCursor(const BCursor* cursor);

			// Same units as BWindow::SetSizeLimits; the server may adjust them.
			status_t			SetSizeLimits(float minWidth, float maxWidth,
									float minHeight, float maxHeight);

			BRect				Frame() const;

			// B_RGB32, sized to the frame; reallocated on a size change.
			// Contents persist across presents. The bitmap may change
			// after Present(): fetch it again for every frame.
			BBitmap*			Buffer();
			void				Present(BRect rect);
			void				Present(const BRegion& region);

private:
			void				_HandleUpdate();
			void				_DrawBuffer(BRect rect);
			BBitmap*			_ChainBuffer();
			bool				_ChainPresent(const BRegion& region);
			void				_ChainDelete();

			PortLink*			fLink;
			port_id				fReplyPort;
			port_id				fEventPort;
			int32				fWindowToken;
			int32				fViewToken;
			BRect				fFrame;
			float				fMinWidth;
			float				fMaxWidth;
			float				fMinHeight;
			float				fMaxHeight;
			BBitmap*			fBuffer;
			// Swap chain of the root view, when VOS_SWAP_CHAIN allows.
			SwapChain*			fChain;
			BBitmap*			fChainBitmaps[4];
			// Where each buffer differs from the one presented last.
			BRegion				fChainStale[4];
			int32				fChainIndex;
			int32				fLastIndex;
			bool				fChainFailed;
			status_t			fInitStatus;
};

}	// namespace BPrivate

using BPrivate::BSurface;


#endif	// _SURFACE_H
