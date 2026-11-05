/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _OUTPUT_ADDON_CLIENT_H
#define _OUTPUT_ADDON_CLIENT_H


#include <OS.h>

#include <OutputAddonProtocol.h>


class BMessage;


// Client library for out-of-process output add-ons: connects to the
// app_server add-on manager, creates or attaches outputs and receives
// damage frames over the shared-buffer transport. One instance drives
// one attached output.
class OutputAddonClient {
public:
								OutputAddonClient();
								~OutputAddonClient();

			status_t			Connect(const char* name = "output add-on");
			void				Disconnect();
			status_t			InitCheck() const;

			team_id				ServerTeam() const { return fServerTeam; }

			status_t			CreateVirtualOutput(int32 width, int32 height,
									const char* name, bool testPattern,
									int32* screen);
			status_t			DeleteVirtualOutput(int32 screen);

			status_t			AttachOutput(int32 screen,
									bool wantInputEvents = false);
			status_t			DetachOutput();

			int32				Screen() const { return fScreen; }
			area_id				FrameBufferArea() const { return fArea; }
			int32				Width() const { return fWidth; }
			int32				Height() const { return fHeight; }
			int32				BytesPerRow() const { return fBytesPerRow; }

			// Blocks for the next frame; damage rects go into the
			// caller's array. B_BAD_PORT_ID means app_server or the
			// subscription is gone; B_STOPPED is the output removed.
			status_t			WaitForFrame(output_addon_frame* frame,
									output_addon_rect* rects,
									bigtime_t timeout = B_INFINITE_TIMEOUT);

			// Copies the published slot's damage into the caller's
			// buffer at the same coordinates, then acks the generation
			// so app_server may reuse the slot.
			status_t			CopyFrame(uint8* dst, int32 dstBytesPerRow);

			// Reverse direction: injects an event into the seat through
			// app_server (remote desktop input).
			status_t			InjectInput(const BMessage& event);

			// Input events app_server forwarded for this add-on.
			status_t			WaitForInput(BMessage* event,
									bigtime_t timeout = B_INFINITE_TIMEOUT);

private:
			status_t			_Request(BMessage& request, BMessage* reply);

			status_t			fInitStatus;
			port_id				fControlPort;
			port_id				fReplyPort;
			port_id				fUpdatePort;
			port_id				fEventPort;
			team_id				fServerTeam;
			int32				fTeam;
			int32				fScreen;
			area_id				fArea;
			void*				fAreaBase;
			output_addon_fb_header* fHeader;
			int32				fBytesPerRow;
			int32				fWidth;
			int32				fHeight;
			output_addon_frame	fLastFrame;
			output_addon_rect	fLastRects[kOutputAddonMaxRects];
};


#endif	// _OUTPUT_ADDON_CLIENT_H
