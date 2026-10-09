/*
 * Copyright 2008-2026, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <GLView.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <syslog.h>

#include <Autolock.h>
#include <Directory.h>
#include <Entry.h>
#include <Path.h>

#include <GLRenderer.h>


// Factory function type exported by renderer add-ons
typedef BGLRenderer* (*renderer_factory_t)(BGLView* view, ulong options,
	BGLDispatcher* dispatcher);


static const char* kRendererDir = "/system/add-ons/opengl";


// Tried in this order. mesa_egl needs a DRM/GBM device; without one its
// factory returns NULL and mesa_surfaceless (llvmpipe-capable) runs.
static const char* kPreferredRenderers[] = {
	"mesa_egl",
	"mesa_surfaceless",
};


// Returns the renderer, or NULL with the add-on unloaded and *failureReason
// set.
static BGLRenderer*
_TryRenderer(const char* path, BGLView* view, ulong options,
	const char** failureReason)
{
	image_id img = load_add_on(path);
	if (img < 0) {
		*failureReason = "add-on failed to load";
		return NULL;
	}

	renderer_factory_t factory = NULL;
	status_t err = get_image_symbol(img, "instantiate_gl_renderer",
		B_SYMBOL_TYPE_TEXT, (void**)&factory);

	if (err == B_OK && factory != NULL) {
		BGLRenderer* renderer = factory(view, options, NULL);
		if (renderer != NULL)
			return renderer;
		*failureReason = "probe failed (no usable DRM/GBM/EGL device)";
	} else
		*failureReason = "no instantiate_gl_renderer symbol";

	unload_add_on(img);
	return NULL;
}


BGLView::BGLView(BRect rect, const char* name, ulong resizingMode,
	ulong mode, ulong options)
	:
	BView(rect, name, resizingMode, mode | B_FRAME_EVENTS | B_WILL_DRAW),
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
	SetViewColor(B_TRANSPARENT_COLOR);
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

	const int32 preferredCount = sizeof(kPreferredRenderers)
		/ sizeof(kPreferredRenderers[0]);
	char failedProbes[256];
	failedProbes[0] = '\0';

	// Preferred pass: strict order, so mesa_egl always wins when its
	// DRM/GBM probe succeeds, and mesa_surfaceless is the silent fallback.
	for (int32 i = 0; i < preferredCount; i++) {
		char path[PATH_MAX];
		snprintf(path, sizeof(path), "%s/%s", kRendererDir,
			kPreferredRenderers[i]);

		BEntry entry(path);
		if (!entry.Exists() || !entry.IsFile())
			continue;

		const char* reason = NULL;
		BGLRenderer* renderer = _TryRenderer(path, this, fOptions, &reason);
		if (renderer != NULL) {
			fRenderer = renderer;
			printf("BGLView: loaded renderer %s\n", path);
			if (failedProbes[0] == '\0') {
				syslog(LOG_INFO, "BGLView: using renderer %s "
					"(preferred)", kPreferredRenderers[i]);
			} else {
				syslog(LOG_INFO, "BGLView: using renderer %s "
					"(fallback after: %s)", kPreferredRenderers[i],
					failedProbes);
			}
			return;
		}

		size_t used = strlen(failedProbes);
		snprintf(failedProbes + used, sizeof(failedProbes) - used,
			"%s%s: %s", used == 0 ? "" : "; ",
			kPreferredRenderers[i], reason);
	}

	// Anything else found in the renderer dir still gets a chance, so a
	// third-party add-on keeps working. Preferred names are not retried.
	BEntry entry;
	while (dir.GetNextEntry(&entry) == B_OK) {
		if (!entry.IsFile())
			continue;

		char name[B_FILE_NAME_LENGTH];
		if (entry.GetName(name) != B_OK)
			continue;

		bool preferred = false;
		for (int32 i = 0; i < preferredCount; i++) {
			if (strcmp(name, kPreferredRenderers[i]) == 0) {
				preferred = true;
				break;
			}
		}
		if (preferred)
			continue;

		BPath path;
		entry.GetPath(&path);

		const char* reason = NULL;
		BGLRenderer* renderer = _TryRenderer(path.Path(), this, fOptions,
			&reason);
		if (renderer != NULL) {
			fRenderer = renderer;
			printf("BGLView: loaded renderer %s\n", path.Path());
			syslog(LOG_INFO, "BGLView: using renderer %s "
				"(non-preferred add-on)", path.Path());
			return;
		}
	}

	fprintf(stderr, "BGLView: no suitable renderer found in %s\n",
		kRendererDir);
	if (failedProbes[0] != '\0')
		syslog(LOG_ERR, "BGLView: no suitable renderer in %s (%s)",
			kRendererDir, failedProbes);
	else
		syslog(LOG_ERR, "BGLView: no suitable renderer in %s",
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
