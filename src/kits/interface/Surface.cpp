/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <Surface.h>

#include <math.h>
#include <string.h>
#include <stdlib.h>

#include <AppServerLink.h>
#include <ApplicationPrivate.h>
#include <Bitmap.h>
#include <Cursor.h>
#include <BitmapPrivate.h>
#include <Message.h>
#include <PortLink.h>
#include <ServerProtocol.h>
#include <ServerProtocolStructs.h>
#include <SwapChain.h>
#include <TokenSpace.h>
#include <View.h>


namespace BPrivate {


BSurface::BSurface(BRect frame, const char* title, window_look look,
	window_feel feel, uint32 flags, uint32 workspaces)
	:
	fLink(NULL),
	fReplyPort(-1),
	fEventPort(-1),
	fWindowToken(B_NULL_TOKEN),
	fViewToken(B_NULL_TOKEN),
	fFrame(frame),
	fMinWidth(0),
	fMaxWidth(32768),
	fMinHeight(0),
	fMaxHeight(32768),
	fBuffer(NULL),
	fChain(NULL),
	fChainIndex(-1),
	fLastIndex(-1),
	fChainFailed(false),
	fInitStatus(B_NO_INIT)
{
	memset(fChainBitmaps, 0, sizeof(fChainBitmaps));
	fReplyPort = create_port(1, "s<app_server");
	// The server drops events when this port is full and never resends an
	// exposure; leave room for an owner that is busy for a while.
	fEventPort = create_port(1024, "surface events");
	if (fReplyPort < B_OK || fEventPort < B_OK) {
		fInitStatus = fReplyPort < B_OK ? fReplyPort : fEventPort;
		return;
	}

	fLink = new(std::nothrow) PortLink(
		BApplication::Private::ServerLink()->SenderPort(), fReplyPort);
	if (fLink == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}

	// Tokens only tell the server's messages apart; nothing looks them up.
	fWindowToken = gDefaultTokens.NewToken(B_HANDLER_TOKEN, NULL);
	fViewToken = gDefaultTokens.NewToken(B_HANDLER_TOKEN, NULL);

	{
		// Keeps others off the application link while it carries our
		// window request.
		AppServerLink lockLink;

		fLink->StartMessage(AS_CREATE_WINDOW);
		fLink->Attach<BRect>(fFrame);
		fLink->Attach<uint32>((uint32)look);
		fLink->Attach<uint32>((uint32)feel);
		fLink->Attach<uint32>(flags);
		fLink->Attach<uint32>(workspaces);
		fLink->Attach<int32>(fWindowToken);
		fLink->Attach<port_id>(fReplyPort);
		fLink->Attach<port_id>(fEventPort);
		fLink->AttachString(title != NULL ? title : "");

		port_id sendPort;
		int32 code;
		if (fLink->FlushWithReply(code) != B_OK || code != B_OK
			|| fLink->Read<port_id>(&sendPort) != B_OK) {
			fInitStatus = B_ERROR;
			return;
		}

		fLink->Read<BRect>(&fFrame);
		fLink->Read<float>(&fMinWidth);
		fLink->Read<float>(&fMaxWidth);
		fLink->Read<float>(&fMinHeight);
		fLink->Read<float>(&fMaxHeight);

		fLink->SetSenderPort(sendPort);
	}

	// One root view covering the window; the server keeps it at the
	// window size. Transparent so the server never erases the content.
	BRect bounds = fFrame.OffsetToCopy(B_ORIGIN);
	fLink->StartMessage(AS_VIEW_CREATE_ROOT);
	fLink->Attach<int32>(fViewToken);
	fLink->AttachString("surface");
	fLink->Attach<BRect>(bounds);
	fLink->Attach<BPoint>(B_ORIGIN);
	fLink->Attach<uint32>(B_FOLLOW_ALL);
	fLink->Attach<uint32>(0);
	fLink->Attach<uint32>(0);
	fLink->Attach<uint32>(B_WILL_DRAW);
	fLink->Attach<bool>(false);
	fLink->Attach<rgb_color>(B_TRANSPARENT_COLOR);
	fLink->Attach<int32>(B_NULL_TOKEN);

	// Without this the server never asks for exposed areas to be redrawn.
	fLink->StartMessage(AS_ENABLE_UPDATES);
	fLink->Flush();

	fInitStatus = B_OK;
}


BSurface::~BSurface()
{
	_ChainDelete();
	if (fLink != NULL && fInitStatus == B_OK) {
		fLink->StartMessage(AS_DELETE_WINDOW);
		int32 code;
		fLink->FlushWithReply(code);
	}

	delete fBuffer;
	delete fLink;

	if (fWindowToken != B_NULL_TOKEN)
		gDefaultTokens.RemoveToken(fWindowToken);
	if (fViewToken != B_NULL_TOKEN)
		gDefaultTokens.RemoveToken(fViewToken);

	if (fReplyPort >= B_OK)
		delete_port(fReplyPort);
	if (fEventPort >= B_OK)
		delete_port(fEventPort);
}


status_t
BSurface::InitCheck() const
{
	return fInitStatus;
}


port_id
BSurface::EventPort() const
{
	return fEventPort;
}


status_t
BSurface::NextEvent(BMessage* event, bigtime_t timeout)
{
	if (fInitStatus != B_OK)
		return fInitStatus;

	ssize_t size = port_buffer_size_etc(fEventPort, B_RELATIVE_TIMEOUT,
		timeout);
	if (size < 0)
		return (status_t)size;

	char* buffer = (char*)malloc(size > 0 ? size : 1);
	if (buffer == NULL)
		return B_NO_MEMORY;

	int32 code;
	ssize_t read = read_port_etc(fEventPort, &code, buffer, size,
		B_RELATIVE_TIMEOUT, 0);
	status_t status = read < 0 ? (status_t)read : event->Unflatten(buffer);
	free(buffer);
	if (status != B_OK)
		return status;

	switch (event->what) {
		case _UPDATE_:
			_HandleUpdate();
			break;

		case B_WINDOW_MOVED:
		{
			BPoint where;
			if (event->FindPoint("where", &where) == B_OK)
				fFrame.OffsetTo(where);
			break;
		}

		case B_WINDOW_RESIZED:
		{
			int32 width;
			int32 height;
			if (event->FindInt32("width", &width) == B_OK
				&& event->FindInt32("height", &height) == B_OK) {
				fFrame.right = fFrame.left + width;
				fFrame.bottom = fFrame.top + height;
			}
			break;
		}
	}

	return B_OK;
}


void
BSurface::MoveTo(BPoint where)
{
	fLink->StartMessage(AS_WINDOW_MOVE);
	fLink->Attach<float>(where.x);
	fLink->Attach<float>(where.y);
	fLink->Flush();
	fFrame.OffsetTo(where);
}


void
BSurface::ResizeTo(float width, float height)
{
	// The server clamps too and sends no event when nothing changes, so the
	// cached frame must match what it will apply.
	width = roundf(width);
	height = roundf(height);
	if (width < fMinWidth)
		width = fMinWidth;
	else if (width > fMaxWidth)
		width = fMaxWidth;
	if (height < fMinHeight)
		height = fMinHeight;
	else if (height > fMaxHeight)
		height = fMaxHeight;

	fLink->StartMessage(AS_WINDOW_RESIZE);
	fLink->Attach<float>(width);
	fLink->Attach<float>(height);
	fLink->Flush();
	fFrame.right = fFrame.left + width;
	fFrame.bottom = fFrame.top + height;
}


void
BSurface::Show()
{
	fLink->StartMessage(AS_SHOW_OR_HIDE_WINDOW);
	fLink->Attach<int32>(0);
	fLink->Flush();
}


void
BSurface::Hide()
{
	fLink->StartMessage(AS_SHOW_OR_HIDE_WINDOW);
	fLink->Attach<int32>(1);
	fLink->Flush();
}


void
BSurface::SetTitle(const char* title)
{
	fLink->StartMessage(AS_SET_WINDOW_TITLE);
	fLink->AttachString(title != NULL ? title : "");
	fLink->Flush();
}


void
BSurface::SetTeam(team_id team)
{
	fLink->StartMessage(AS_SET_WINDOW_TEAM);
	fLink->Attach<team_id>(team);
	fLink->Flush();
}


status_t
BSurface::SetLook(window_look look)
{
	fLink->StartMessage(AS_SET_LOOK);
	fLink->Attach<int32>((int32)look);
	status_t status = B_ERROR;
	if (fLink->FlushWithReply(status) != B_OK)
		return B_ERROR;
	return status;
}


status_t
BSurface::SetFeel(window_feel feel)
{
	fLink->StartMessage(AS_SET_FEEL);
	fLink->Attach<int32>((int32)feel);
	status_t status = B_ERROR;
	if (fLink->FlushWithReply(status) != B_OK)
		return B_ERROR;
	return status;
}


status_t
BSurface::SetFlags(uint32 flags)
{
	fLink->StartMessage(AS_SET_FLAGS);
	fLink->Attach<uint32>(flags);
	int32 status = B_ERROR;
	if (fLink->FlushWithReply(status) != B_OK)
		return B_ERROR;
	return status;
}


void
BSurface::Activate(bool active)
{
	fLink->StartMessage(AS_ACTIVATE_WINDOW);
	fLink->Attach<bool>(active);
	fLink->Flush();
}


void
BSurface::Minimize(bool minimize)
{
	fLink->StartMessage(AS_MINIMIZE_WINDOW);
	fLink->Attach<bool>(minimize);
	fLink->Flush();
}


void
BSurface::SetEventMask(uint32 mask, uint32 options)
{
	fLink->StartMessage(AS_VIEW_SET_EVENT_MASK);
	fLink->Attach<uint32>(mask);
	fLink->Attach<uint32>(options);
	fLink->Flush();
}


void
BSurface::SetCursor(const BCursor* cursor)
{
	if (cursor == NULL || fInitStatus != B_OK)
		return;

	ViewSetViewCursorInfo info;
	info.cursorToken = cursor->fServerToken;
	info.viewToken = fViewToken;
	info.sync = true;

	// The server keeps the cursor alive; wait so the caller may delete it.
	AppServerLink link;
	link.StartMessage(AS_SET_VIEW_CURSOR);
	link.Attach<ViewSetViewCursorInfo>(info);
	int32 code;
	link.FlushWithReply(code);
}


status_t
BSurface::SetSizeLimits(float minWidth, float maxWidth, float minHeight,
	float maxHeight)
{
	if (fInitStatus != B_OK)
		return fInitStatus;
	if (minWidth > maxWidth || minHeight > maxHeight)
		return B_BAD_VALUE;

	fLink->StartMessage(AS_SET_SIZE_LIMITS);
	fLink->Attach<float>(minWidth);
	fLink->Attach<float>(maxWidth);
	fLink->Attach<float>(minHeight);
	fLink->Attach<float>(maxHeight);

	// The reply carries the limits and frame the server really applied.
	int32 code;
	status_t status = fLink->FlushWithReply(code);
	if (status != B_OK)
		return status;
	if (code != B_OK)
		return code;

	fLink->Read<BRect>(&fFrame);
	fLink->Read<float>(&fMinWidth);
	fLink->Read<float>(&fMaxWidth);
	fLink->Read<float>(&fMinHeight);
	fLink->Read<float>(&fMaxHeight);
	return B_OK;
}


BRect
BSurface::Frame() const
{
	return fFrame;
}


BBitmap*
BSurface::Buffer()
{
	if (fInitStatus == B_OK && !fChainFailed && swap_chain_enabled()) {
		// Chain buffers may live in VRAM: they are only written, from fBuffer.
		_ChainBuffer();
	}

	BRect bounds(0, 0, fFrame.IntegerWidth(), fFrame.IntegerHeight());
	if (fBuffer != NULL && fBuffer->Bounds() == bounds)
		return fBuffer;

	delete fBuffer;
	fBuffer = new(std::nothrow) BBitmap(bounds, B_RGB32);
	if (fBuffer != NULL && fBuffer->InitCheck() != B_OK) {
		delete fBuffer;
		fBuffer = NULL;
	}
	return fBuffer;
}


void
BSurface::Present(BRect rect)
{
	if (fChain != NULL && _ChainPresent(BRegion(rect)))
		return;

	_DrawBuffer(rect);
	fLink->Flush();
}


void
BSurface::Present(const BRegion& region)
{
	if (fChain != NULL && _ChainPresent(region))
		return;

	for (int32 i = 0; i < region.CountRects(); i++)
		_DrawBuffer(region.RectAt(i));
	fLink->Flush();
}


bool
BSurface::ExternalReady()
{
	if (fInitStatus != B_OK || fChainFailed || !swap_chain_enabled())
		return false;
	if (fChain == NULL && _ChainBuffer() == NULL)
		return false;
	return fChain != NULL && fChain->IsValid() && !fChain->ExternalRefused();
}


bool
BSurface::ExternalImport(int fd, const swap_chain_external& buffer,
	int32* slot)
{
	return fChain != NULL && fChain->ExternalImport(fd, buffer, slot) == B_OK;
}


void
BSurface::ExternalReset()
{
	if (fChain != NULL)
		fChain->ExternalReset();
}


bool
BSurface::PresentExternal(int32 slot, const BRegion& region)
{
	if (fChain == NULL)
		return false;

	BRect rects[kSwapChainMaxDamage];
	int32 count = 0;
	const BRect bounds(0, 0, fChain->Width() - 1, fChain->Height() - 1);
	for (int32 i = 0; i < region.CountRects(); i++) {
		const BRect rect = region.RectAt(i) & bounds;
		if (!rect.IsValid())
			continue;
		if (count == kSwapChainMaxDamage) {
			count = 0;
			break;
		}
		rects[count++] = rect;
	}
	if (fChain->PresentExternal(slot, rects, count) != B_OK)
		return false;

	// The screen now holds what the chain buffers do not.
	for (int32 i = 0; i < 4; i++)
		fChainStale[i].Set(bounds);
	return true;
}


bool
BSurface::ExternalReleased(int32 slot) const
{
	return fChain != NULL && fChain->ExternalReleased(slot);
}


bool
BSurface::ExternalRefused() const
{
	return fChain != NULL && fChain->ExternalRefused();
}


int32
BSurface::RefreshPeriod() const
{
	return fChain != NULL ? fChain->RefreshPeriod() : 0;
}


// The chain buffer to render into, or NULL for the bitmap path.
BBitmap*
BSurface::_ChainBuffer()
{
	const int32 width = fFrame.IntegerWidth() + 1;
	const int32 height = fFrame.IntegerHeight() + 1;

	if (fChain != NULL && (fChain->Width() != width
			|| fChain->Height() != height)) {
		for (int32 i = 0; i < 4; i++) {
			delete fChainBitmaps[i];
			fChainBitmaps[i] = NULL;
		}
		fChainIndex = -1;
		fLastIndex = -1;
		for (int32 i = 0; i < 4; i++)
			fChainStale[i].MakeEmpty();
		if (fChain->Resize(width, height) != B_OK)
			_ChainDelete();
	}

	if (fChain == NULL && !fChainFailed) {
		fChain = new(std::nothrow) SwapChain;
		const swap_chain_format format = { kSwapFormatXRGB8888,
			kSwapModifierLinear };
		if (fChain == NULL || fChain->Create(fLink, fViewToken, width, height,
				4, kSwapUsageRender, &format, 1) != B_OK)
			_ChainDelete();
	}
	if (fChain == NULL) {
		fChainFailed = true;
		return NULL;
	}

	if (fChainIndex < 0) {
		const int32 index = fChain->Acquire(1000000);
		if (index < 0) {
			_ChainDelete();
			fChainFailed = true;
			return NULL;
		}
		fChainIndex = index;
	}

	for (int32 i = 0; i < fChain->CountBuffers(); i++) {
		if (fChainBitmaps[i] != NULL)
			continue;

		swap_chain_buffer buffer;
		if (fChain->BufferAt(i, &buffer) != B_OK)
			break;
		fChainBitmaps[i] = new(std::nothrow) BBitmap(buffer.area,
			buffer.offset, BRect(0, 0, width - 1, height - 1),
			B_BITMAP_NO_SERVER_LINK, B_RGB32, buffer.stride);
		if (fChainBitmaps[i] != NULL && fChainBitmaps[i]->InitCheck() != B_OK) {
			delete fChainBitmaps[i];
			fChainBitmaps[i] = NULL;
		}
		if (fChainBitmaps[i] != NULL)
			fChainStale[i].Set(BRect(0, 0, width - 1, height - 1));
	}

	BBitmap* bitmap = fChainBitmaps[fChainIndex];
	if (bitmap == NULL) {
		_ChainDelete();
		fChainFailed = true;
		return NULL;
	}

	return bitmap;
}


bool
BSurface::_ChainPresent(const BRegion& region)
{
	if (fChain == NULL)
		return false;

	// Presenting again without drawing shows the same contents.
	if (Buffer() == NULL || fChain == NULL || fChainIndex < 0)
		return false;

	BRect rects[kSwapChainMaxDamage];
	int32 count = 0;
	bool overflow = false;
	const BRect bounds(0, 0, fChain->Width() - 1, fChain->Height() - 1);
	for (int32 i = 0; i < region.CountRects(); i++) {
		const BRect rect = region.RectAt(i) & bounds;
		if (!rect.IsValid())
			continue;
		if (count == kSwapChainMaxDamage) {
			// Too many: the whole buffer.
			overflow = true;
			break;
		}
		rects[count++] = rect;
	}
	if (overflow)
		count = 0;
	else if (count == 0)
		return true;

	// Write what changed, plus what this buffer missed, from system memory.
	BRegion copy(fChainStale[fChainIndex]);
	if (count == 0)
		copy.Set(bounds);
	else {
		for (int32 i = 0; i < count; i++)
			copy.Include(rects[i]);
	}
	BBitmap* src = fBuffer;
	BBitmap* dst = fChainBitmaps[fChainIndex];
	if (src == NULL || dst == NULL || src->Bounds() != bounds)
		return false;
	const int32 srcStride = src->BytesPerRow();
	const int32 dstStride = dst->BytesPerRow();
	for (int32 i = 0; i < copy.CountRects(); i++) {
		const BRect rect = copy.RectAt(i) & bounds;
		if (!rect.IsValid())
			continue;

		const int32 offset = (int32)rect.left * 4;
		const int32 length = min_c(((int32)rect.IntegerWidth() + 1) * 4,
			min_c(srcStride, dstStride) - offset);
		if (length <= 0)
			continue;

		for (int32 y = (int32)rect.top; y <= (int32)rect.bottom; y++) {
			memcpy((uint8*)dst->Bits() + y * dstStride + offset,
				(uint8*)src->Bits() + y * srcStride + offset, length);
		}
	}

	if (fChain->Present(fChainIndex, rects, count, BPrivate::kPresentCpuWrite)
			!= B_OK)
		return true;

	// Every other buffer misses what this one changed.
	for (int32 i = 0; i < 4; i++) {
		if (i == fChainIndex)
			continue;

		if (count == 0)
			fChainStale[i].Set(bounds);
		else {
			for (int32 j = 0; j < count; j++)
				fChainStale[i].Include(rects[j]);
		}
	}
	fChainStale[fChainIndex].MakeEmpty();

	fLastIndex = fChainIndex;
	fChainIndex = -1;
	return true;
}


void
BSurface::_ChainDelete()
{
	for (int32 i = 0; i < 4; i++) {
		delete fChainBitmaps[i];
		fChainBitmaps[i] = NULL;
	}
	delete fChain;
	fChain = NULL;
	fChainIndex = -1;
	fLastIndex = -1;
	for (int32 i = 0; i < 4; i++)
		fChainStale[i].MakeEmpty();
}


void
BSurface::_HandleUpdate()
{
	fLink->StartMessage(AS_BEGIN_UPDATE);

	int32 code;
	if (fLink->FlushWithReply(code) == B_OK && code == B_OK) {
		BPoint origin;
		float width;
		float height;
		fLink->Read<BPoint>(&origin);
		fLink->Read<float>(&width);
		fLink->Read<float>(&height);
		fFrame.Set(origin.x, origin.y, origin.x + width, origin.y + height);

		// Read every token first; drawing in between would interleave
		// with the reply on the link.
		BRegion dirty;
		while (true) {
			int32 token;
			BRect rect;
			if (fLink->Read<int32>(&token) != B_OK || token == B_NULL_TOKEN
				|| fLink->Read<BRect>(&rect) != B_OK) {
				break;
			}
			// Update rects arrive in screen coordinates.
			dirty.Include(rect.OffsetByCopy(-origin.x, -origin.y));
		}

		for (int32 i = 0; i < dirty.CountRects(); i++)
			_DrawBuffer(dirty.RectAt(i));
	}

	fLink->StartMessage(AS_END_UPDATE);
	fLink->Flush();
}


void
BSurface::_DrawBuffer(BRect rect)
{
	if (fBuffer == NULL)
		return;

	rect = rect & fBuffer->Bounds();
	if (!rect.IsValid())
		return;

	ViewDrawBitmapInfo info;
	info.bitmapToken = BBitmap::Private(fBuffer).ServerToken();
	info.options = 0;
	info.viewRect = rect;
	info.bitmapRect = rect;

	fLink->StartMessage(AS_VIEW_DRAW_BITMAP);
	fLink->Attach<ViewDrawBitmapInfo>(info);
}


}	// namespace BPrivate
