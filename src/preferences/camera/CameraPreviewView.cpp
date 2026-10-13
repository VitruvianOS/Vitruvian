/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "CameraPreviewView.h"

#include <string.h>
#include <vector>

#include <Autolock.h>
#include <Bitmap.h>
#include <Message.h>
#include <Rect.h>
#include <View.h>
#include <Window.h>

#include "FormatConversion.h"
#include "CameraMessages.h"


using BPrivate::media::ConvertI420ToRGB32;
using BPrivate::media::ConvertYUY2ToRGB32;


CameraPreviewView::CameraPreviewView(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS),
	fCamera(NULL),
	fMirror(false),
	fLock("camera preview"),
	fBitmap(NULL)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
}


void
CameraPreviewView::AttachedToWindow()
{
	if (fCamera != NULL)
		fCamera->SetFrameTarget(BMessenger(this));
}


void
CameraPreviewView::DetachedFromWindow()
{
	if (fCamera != NULL)
		fCamera->SetFrameTarget(BMessenger());
}


void
CameraPreviewView::SetCamera(BCameraSource* camera)
{
	{
		BAutolock _(fLock);
		fCamera = camera;
		delete fBitmap;
		fBitmap = NULL;
	}
	// No camera attached is a normal state, not an error.
	if (fCamera != NULL && Window() != NULL)
		fCamera->SetFrameTarget(BMessenger(this));
	Invalidate();
}


void
CameraPreviewView::SetMirror(bool mirror)
{
	fMirror = mirror;
	Invalidate();
}


// Flip packed pixels left-to-right before colour conversion.
static void
_FlipYUY2(const uint8* src, uint8* dst, uint32 width, uint32 height)
{
	const uint32 bpr = width * 2;
	for (uint32 y = 0; y < height; y++) {
		const uint8* row = src + (size_t)y * bpr;
		uint8* out = dst + (size_t)y * bpr;
		for (uint32 x = 0; x < width; x += 2)
			memcpy(out + (size_t)(width - 2 - x) * 2, row + (size_t)x * 2, 4);
	}
}


static void
_FlipI420(const uint8* src, uint8* dst, uint32 width, uint32 height)
{
	const size_t ySize = (size_t)width * height;
	const uint32 cW = width / 2;
	const uint32 cH = height / 2;
	const size_t cSize = (size_t)cW * cH;
	const uint8* uSrc = src + ySize;
	const uint8* vSrc = uSrc + cSize;

	for (uint32 y = 0; y < height; y++) {
		const uint8* row = src + (size_t)y * width;
		uint8* out = dst + (size_t)y * width;
		for (uint32 x = 0; x < width; x++)
			out[x] = row[width - 1 - x];
	}
	for (uint32 y = 0; y < cH; y++) {
		const uint8* uRow = uSrc + (size_t)y * cW;
		const uint8* vRow = vSrc + (size_t)y * cW;
		uint8* uOut = dst + ySize + (size_t)y * cW;
		uint8* vOut = dst + ySize + cSize + (size_t)y * cW;
		for (uint32 x = 0; x < cW; x++) {
			uOut[x] = uRow[cW - 1 - x];
			vOut[x] = vRow[cW - 1 - x];
		}
	}
}


void
CameraPreviewView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_CAMERA_FRAME:
		{
			BCameraSource* camera = fCamera;
			if (camera == NULL)
				break;

			media_raw_video_format format;
			memset(&format, 0, sizeof(format));
			std::vector<uint8> buffer(8 * 1024 * 1024);
			if (camera->CopyLastFrame(buffer.data(), buffer.size(), &format)
					!= B_OK
				|| format.width == 0 || format.height == 0)
				break;

			const uint32 width = format.width;
			const uint32 height = format.height;
			const uint32 bpr = width * 4;
			const size_t needed = (size_t)bpr * height;
			if (needed > buffer.size())
				break;

			BAutolock _(fLock);
			delete fBitmap;
			fBitmap = new BBitmap(BRect(0, 0, width - 1, height - 1),
				B_RGB32, true);
			if (fBitmap == NULL || fBitmap->InitCheck() != B_OK) {
				delete fBitmap;
				fBitmap = NULL;
				break;
			}

			// CPU-mapped frames are BGRx / YUY2 / I420 per the negotiated
			// SPA format; convert to B_RGB32 for the BBitmap.
			bool converted = false;
			uint8* bits = (uint8*)fBitmap->Bits();
			const uint32 dstStride = fBitmap->BytesPerRow();
			if (format.color_space == B_RGB32) {
				for (uint32 y = 0; y < height; y++) {
					uint8* dst = bits + y * dstStride;
					const uint8* src = buffer.data() + (size_t)y * bpr;
					if (fMirror) {
						for (uint32 x = 0; x < width; x++) {
							const uint8* s = src + (size_t)(width - 1 - x) * 4;
							dst[x * 4 + 0] = s[0];
							dst[x * 4 + 1] = s[1];
							dst[x * 4 + 2] = s[2];
							dst[x * 4 + 3] = 0xff;
						}
					} else {
						memcpy(dst, src, bpr);
						for (uint32 x = 0; x < width; x++)
							dst[x * 4 + 3] = 0xff;
					}
				}
				converted = true;
			} else if (format.color_space == B_YCbCr422
				|| format.color_space == B_YUV420) {
				const size_t srcSize = format.color_space == B_YCbCr422
					? (size_t)width * height * 2
					: (size_t)width * height * 3 / 2;
				const uint8* src = buffer.data();
				std::vector<uint8> flipped;
				if (fMirror) {
					flipped.resize(srcSize);
					if (format.color_space == B_YCbCr422)
						_FlipYUY2(src, flipped.data(), width, height);
					else
						_FlipI420(src, flipped.data(), width, height);
					src = flipped.data();
				}
				if (format.color_space == B_YCbCr422)
					converted = ConvertYUY2ToRGB32(src, width, height,
						bits, dstStride);
				else
					converted = ConvertI420ToRGB32(src, width, height,
						bits, dstStride);
			}
			if (!converted) {
				delete fBitmap;
				fBitmap = NULL;
			}
			Invalidate();
			break;
		}
		default:
			BView::MessageReceived(message);
			break;
	}
}


void
CameraPreviewView::Draw(BRect updateRect)
{
	BView::Draw(updateRect);

	BAutolock _(fLock);
	if (fBitmap == NULL) {
		SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
		DrawString("Preview unavailable", BPoint(20, Bounds().Height() / 2));
		return;
	}

	DrawBitmap(fBitmap, Bounds());
}
