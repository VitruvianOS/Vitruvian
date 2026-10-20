/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "CortexMonitor.h"

#include <Autolock.h>
#include <LayoutBuilder.h>
#include <Message.h>

#include <string.h>

#include "CortexWindow.h"


static const uint32 kMsgFrame = 'CMFR';


CortexMonitorView::CortexMonitorView(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fLock("cortex monitor view"),
	fFrame(NULL),
	fUpdatePending(false)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	SetExplicitMinSize(BSize(160, 120));
	SetExplicitPreferredSize(BSize(640, 480));
}


CortexMonitorView::~CortexMonitorView()
{
	delete fFrame;
}


void
CortexMonitorView::SetFrame(const uint8* bits, size_t size, uint32 width,
	uint32 height)
{
	if (bits == NULL || width == 0 || height == 0)
		return;

	bool post = false;
	{
		BAutolock _(fLock);
		if (fFrame == NULL
			|| (uint32)fFrame->Bounds().Width() + 1 != width
			|| (uint32)fFrame->Bounds().Height() + 1 != height) {
			delete fFrame;
			fFrame = new(std::nothrow) BBitmap(
				BRect(0, 0, width - 1, height - 1), B_RGB32);
			if (fFrame == NULL || fFrame->InitCheck() != B_OK) {
				delete fFrame;
				fFrame = NULL;
				return;
			}
		}
		const size_t frameSize = (size_t)fFrame->BytesPerRow() * height;
		if (size >= frameSize)
			memcpy(fFrame->Bits(), bits, frameSize);
		post = !fUpdatePending;
		fUpdatePending = true;
	}

	// One redraw in flight at a time keeps a fast source from flooding
	// the window queue.
	if (post && Window() != NULL)
		Window()->PostMessage(kMsgFrame, this);
}


void
CortexMonitorView::MessageReceived(BMessage* message)
{
	if (message->what != kMsgFrame) {
		BView::MessageReceived(message);
		return;
	}

	{
		BAutolock _(fLock);
		fUpdatePending = false;
	}
	Invalidate();
}


void
CortexMonitorView::Draw(BRect updateRect)
{
	BRect bounds = Bounds();
	SetLowColor(0, 0, 0);

	BAutolock _(fLock);
	if (fFrame == NULL) {
		FillRect(bounds, B_SOLID_LOW);
		return;
	}

	// Scale to fit, keep the aspect ratio, bar the rest.
	BRect source = fFrame->Bounds();
	float scale = bounds.Width() / (source.Width() + 1.0f);
	float vertical = bounds.Height() / (source.Height() + 1.0f);
	if (vertical < scale)
		scale = vertical;

	float width = (source.Width() + 1.0f) * scale;
	float height = (source.Height() + 1.0f) * scale;
	BRect target(0, 0, width - 1.0f, height - 1.0f);
	target.OffsetTo((bounds.Width() - width) / 2.0f,
		(bounds.Height() - height) / 2.0f);

	if (target.left > 0)
		FillRect(BRect(0, 0, target.left - 1.0f, bounds.bottom), B_SOLID_LOW);
	if (target.right < bounds.right)
		FillRect(BRect(target.right + 1.0f, 0, bounds.right, bounds.bottom),
			B_SOLID_LOW);
	if (target.top > 0)
		FillRect(BRect(target.left, 0, target.right, target.top - 1.0f),
			B_SOLID_LOW);
	if (target.bottom < bounds.bottom)
		FillRect(BRect(target.left, target.bottom + 1.0f, target.right,
			bounds.bottom), B_SOLID_LOW);

	SetDrawingMode(B_OP_COPY);
	DrawBitmap(fFrame, source, target, B_FILTER_BITMAP_BILINEAR);
}


/* static */ void
CortexMonitorView::FrameHook(void* cookie, bigtime_t /*timestamp*/, void* data,
	size_t size, const BMediaFormat& format)
{
	if (!format.IsRawVideo())
		return;
	((CortexMonitorView*)cookie)->SetFrame((const uint8*)data, size,
		format.format.u.raw_video.width, format.format.u.raw_video.height);
}


CortexMonitorWindow::CortexMonitorWindow(const char* title,
	const BMessenger& owner)
	:
	BWindow(BRect(120, 120, 760, 600), title, B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fView(NULL),
	fOwner(owner)
{
	fView = new CortexMonitorView("monitor");
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(0)
		.Add(fView);
}


bool
CortexMonitorWindow::QuitRequested()
{
	// The owner unlinks the tap first, then quits this window.
	if (fOwner.IsValid()) {
		BMessage closed(kMsgMonitorClosed);
		closed.AddPointer("window", this);
		fOwner.SendMessage(&closed);
		Hide();
		return false;
	}
	return true;
}
