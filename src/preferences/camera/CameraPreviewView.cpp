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

#include "CameraMessages.h"


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
	if (Window() != NULL)
		fCamera->SetFrameTarget(BMessenger(this));
	Invalidate();
}


void
CameraPreviewView::SetMirror(bool mirror)
{
	fMirror = mirror;
	Invalidate();
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

			// CPU-mapped frames are BGRx / YUY2 / I420 per FormatConversion.
			// Preview v1 draws BGRx directly; other formats show placeholder.
			if (format.color_space == B_RGB32) {
				uint8* bits = (uint8*)fBitmap->Bits();
				for (uint32 y = 0; y < height; y++) {
					uint8* dst = bits + y * fBitmap->BytesPerRow();
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
			} else {
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
