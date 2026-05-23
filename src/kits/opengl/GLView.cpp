/*
 * Copyright 2008-2026, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <GLView.h>

#include <stdio.h>
#include <string.h>

#include <Autolock.h>
#include <Directory.h>
#include <Entry.h>
#include <Path.h>

#include <GLRenderer.h>


// Factory function type exported by renderer add-ons
typedef BGLRenderer* (*renderer_factory_t)(BGLView* view, ulong options,
	BGLDispatcher* dispatcher);


static const char* kRendererDir = "/system/add-ons/opengl";


BGLView::BGLView(BRect rect, const char* name, ulong resizingMode,
	ulong mode, ulong options)
	:
	BView(rect, name, resizingMode, mode | B_FRAME_EVENTS),
	fGc(NULL),
	fOptions(options),
	fDitherCount(0),
	fDrawLock("BGLView draw lock"),
	fDisplayLock("BGLView display lock"),
	fClipInfo(NULL),
	fRenderer(NULL),
	fRoster(NULL),
	fDitherMap(NULL),
	fBounds(rect)
{
	memset(fErrorBuffer, 0, sizeof(fErrorBuffer));
	_LoadRenderer();
}


// BeOS compat constructor
BGLView::BGLView(BRect rect, char* name, ulong resizingMode,
	ulong mode, ulong options)
	:
	BGLView(rect, (const char*)name, resizingMode, mode, options)
{
}


BGLView::~BGLView()
{
	if (fRenderer) {
		fRenderer->Release();
		fRenderer = NULL;
	}
}


void
BGLView::LockGL()
{
	fDisplayLock.Lock();
	if (fRenderer)
		fRenderer->LockGL();
}


void
BGLView::UnlockGL()
{
	if (fRenderer)
		fRenderer->UnlockGL();
	fDisplayLock.Unlock();
}


void
BGLView::SwapBuffers()
{
	SwapBuffers(false);
}


void
BGLView::SwapBuffers(bool vSync)
{
	BAutolock _(fDisplayLock);
	if (fRenderer)
		fRenderer->SwapBuffers(vSync);
}


BView*
BGLView::EmbeddedView()
{
	return NULL;
}


void*
BGLView::GetGLProcAddress(const char* procName)
{
	// Delegate to renderer if available
	return NULL;
}


status_t
BGLView::CopyPixelsOut(BPoint source, BBitmap* dest)
{
	BAutolock _(fDisplayLock);
	if (!fRenderer)
		return B_ERROR;
	return fRenderer->CopyPixelsOut(source, dest);
}


status_t
BGLView::CopyPixelsIn(BBitmap* source, BPoint dest)
{
	BAutolock _(fDisplayLock);
	if (!fRenderer)
		return B_ERROR;
	return fRenderer->CopyPixelsIn(source, dest);
}


void
BGLView::ErrorCallback(unsigned long errorCode)
{
	fprintf(stderr, "BGLView::ErrorCallback: GL error 0x%lx\n", errorCode);
}


void
BGLView::Draw(BRect updateRect)
{
	if (fRenderer)
		fRenderer->Draw(updateRect);
}


void
BGLView::AttachedToWindow()
{
	BView::AttachedToWindow();
}


void
BGLView::AllAttached()
{
	BView::AllAttached();
}


void
BGLView::DetachedFromWindow()
{
	BView::DetachedFromWindow();
}


void
BGLView::AllDetached()
{
	BView::AllDetached();
}


void
BGLView::FrameResized(float newWidth, float newHeight)
{
	BAutolock _(fDisplayLock);
	if (fRenderer)
		fRenderer->FrameResized(newWidth, newHeight);
	BView::FrameResized(newWidth, newHeight);
}


status_t
BGLView::Perform(perform_code /*d*/, void* /*arg*/)
{
	return B_ERROR;
}


status_t
BGLView::Archive(BMessage* data, bool deep) const
{
	return BView::Archive(data, deep);
}


void
BGLView::MessageReceived(BMessage* message)
{
	BView::MessageReceived(message);
}


void
BGLView::SetResizingMode(uint32 mode)
{
	BView::SetResizingMode(mode);
}


void
BGLView::Show()
{
	BView::Show();
}


void
BGLView::Hide()
{
	BView::Hide();
}


BHandler*
BGLView::ResolveSpecifier(BMessage* msg, int32 index, BMessage* specifier,
	int32 form, const char* property)
{
	return BView::ResolveSpecifier(msg, index, specifier, form, property);
}


status_t
BGLView::GetSupportedSuites(BMessage* data)
{
	return BView::GetSupportedSuites(data);
}


void
BGLView::DirectConnected(direct_buffer_info* info)
{
	if (fRenderer)
		fRenderer->DirectConnected(info);
}


void
BGLView::EnableDirectMode(bool enabled)
{
	if (fRenderer)
		fRenderer->EnableDirectMode(enabled);
}


void
BGLView::GetPreferredSize(float* width, float* height)
{
	BView::GetPreferredSize(width, height);
}


void
BGLView::_LockDraw()
{
	fDrawLock.Lock();
}


void
BGLView::_UnlockDraw()
{
	fDrawLock.Unlock();
}


void
BGLView::_LoadRenderer()
{
	BDirectory dir(kRendererDir);
	if (dir.InitCheck() != B_OK) {
		fprintf(stderr, "BGLView: renderer dir not found: %s\n", kRendererDir);
		return;
	}

	BEntry entry;
	while (dir.GetNextEntry(&entry) == B_OK) {
		if (!entry.IsFile())
			continue;

		BPath path;
		entry.GetPath(&path);

		image_id img = load_add_on(path.Path());
		if (img < 0)
			continue;

		renderer_factory_t factory = NULL;
		status_t err = get_image_symbol(img, "instantiate_gl_renderer",
			B_SYMBOL_TYPE_TEXT, (void**)&factory);

		if (err == B_OK && factory != NULL) {
			fRenderer = factory(this, fOptions, NULL);
			if (fRenderer != NULL) {
				printf("BGLView: loaded renderer %s\n", path.Path());
				return;
			}
		}

		unload_add_on(img);
	}

	fprintf(stderr, "BGLView: no suitable renderer found in %s\n",
		kRendererDir);
}


// Virtual reserved slots
void BGLView::_ReservedGLView1() {}
void BGLView::_ReservedGLView2() {}
void BGLView::_ReservedGLView3() {}
void BGLView::_ReservedGLView4() {}
void BGLView::_ReservedGLView5() {}
void BGLView::_ReservedGLView6() {}
void BGLView::_ReservedGLView7() {}
void BGLView::_ReservedGLView8() {}


// ---------------------------------------------------------------------------
// BGLScreen stub implementation
// ---------------------------------------------------------------------------

BGLScreen::BGLScreen(char* name, ulong screenMode, ulong options,
	status_t* error, bool /*debug*/)
	:
	BWindowScreen(name, screenMode, error),
	fGc(NULL),
	fOptions(options),
	fDrawLock("BGLScreen draw lock"),
	fColorSpace(B_RGB32),
	fScreenMode(screenMode)
{
}


BGLScreen::~BGLScreen()
{
}


void BGLScreen::LockGL() { fDrawLock.Lock(); }
void BGLScreen::UnlockGL() { fDrawLock.Unlock(); }
void BGLScreen::SwapBuffers() {}
void BGLScreen::ErrorCallback(unsigned long) {}
void BGLScreen::ScreenConnected(bool) {}
void BGLScreen::FrameResized(float, float) {}


status_t
BGLScreen::Perform(perform_code, void*)
{
	return B_ERROR;
}


status_t
BGLScreen::Archive(BMessage* data, bool deep) const
{
	return BWindowScreen::Archive(data, deep);
}


void BGLScreen::MessageReceived(BMessage* msg) { BWindowScreen::MessageReceived(msg); }
void BGLScreen::Show() { BWindowScreen::Show(); }
void BGLScreen::Hide() { BWindowScreen::Hide(); }


BHandler*
BGLScreen::ResolveSpecifier(BMessage* msg, int32 index, BMessage* spec,
	int32 form, const char* prop)
{
	return BWindowScreen::ResolveSpecifier(msg, index, spec, form, prop);
}


status_t
BGLScreen::GetSupportedSuites(BMessage* data)
{
	return BWindowScreen::GetSupportedSuites(data);
}


void BGLScreen::_ReservedGLScreen1() {}
void BGLScreen::_ReservedGLScreen2() {}
void BGLScreen::_ReservedGLScreen3() {}
void BGLScreen::_ReservedGLScreen4() {}
void BGLScreen::_ReservedGLScreen5() {}
void BGLScreen::_ReservedGLScreen6() {}
void BGLScreen::_ReservedGLScreen7() {}
void BGLScreen::_ReservedGLScreen8() {}
