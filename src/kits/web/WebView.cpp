/*
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * Distributed under the terms of the LGPL License.
 *
 * BWebView: BView wrapper for the WPE WebKit2 compositor bridge.
 */

#include <web/WebView.h>
#include <web/WebDefs.h>

#include <Application.h>
#include <Bitmap.h>
#include <Message.h>
#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include <cstdio>
#include <cstring>

#ifdef HAS_WPE
#include "compositor_adapter.h"
#include "event_adapter.h"
#include "frame_notifier.h"
#include "webkit_bridge.h"
#include "process_manager.h"


static const uint32 kMsgFrameReady = 'wfrm';
#include "session_adapter.h"
#endif


struct BWebView::WebViewImpl {
	BString		fCurrentURL;
	BString		fTitle;
	double		fProgress;
	bool		fLoading;
	BView*		fHostView;

#ifdef HAS_WPE
	CompositorAdapter*	fCompositor;
	EventAdapter*		fEventAdapter;
	WebKitBridge*		fBridge;
	FrameNotifier*		fNotifier;
	BMessenger			fSelfMessenger;
#endif

	WebViewImpl()
		:
		fProgress(0.0),
		fLoading(false),
		fHostView(NULL)
#ifdef HAS_WPE
		,
		fCompositor(NULL),
		fEventAdapter(NULL),
		fBridge(NULL),
		fNotifier(NULL)
#endif
	{
	}
};


BWebView::BWebView(BRect frame, const char* name)
	:
	BView(frame, name, B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP,
		B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fImpl(new WebViewImpl())
{
}


BWebView::~BWebView()
{
#ifdef HAS_WPE
	delete fImpl->fNotifier;
	delete fImpl->fBridge;
	delete fImpl->fEventAdapter;
#endif
	delete fImpl;
}


void
BWebView::AttachedToWindow()
{
	BView::AttachedToWindow();
	fImpl->fHostView = this;

#ifdef HAS_WPE
	// Start the GLib event loop if not already running.
	ProcessManager::Instance().StartGLibLoop();

	int w = (int)(Bounds().Width() + 1);
	int h = (int)(Bounds().Height() + 1);

	fImpl->fBridge = new WebKitBridge(w, h);
	if (!fImpl->fBridge->IsValid()) {
		delete fImpl->fBridge;
		fImpl->fBridge = NULL;
		return;
	}

	fImpl->fCompositor = fImpl->fBridge->compositor.get();
	fImpl->fEventAdapter = new EventAdapter(fImpl->fBridge->backend);
	fImpl->fNotifier = new FrameNotifier();
	// Frames arrive on the GLib thread: ask the window thread to redraw
	// instead of taking the window lock from there.
	BMessenger self(this);
	fImpl->fBridge->compositor->onFrameReady = [self]() {
		self.SendMessage(kMsgFrameReady);
	};

	// Wire bridge callbacks to BWebView target messenger.
	fImpl->fBridge->onTitleChanged = [this](const std::string& t) {
		fImpl->fTitle.SetTo(t.c_str());
		if (fTarget.IsValid()) {
			BMessage msg(B_WEB_PAGE_TITLE_CHANGED);
			msg.AddString(B_WEB_TITLE_FIELD, t.c_str());
			fTarget.SendMessage(&msg);
		}
	};

	fImpl->fBridge->onURLChanged = [this](const std::string& u) {
		fImpl->fCurrentURL.SetTo(u.c_str());
		if (fTarget.IsValid()) {
			BMessage msg(B_WEB_PAGE_URL_CHANGED);
			msg.AddString(B_WEB_URL_FIELD, u.c_str());
			fTarget.SendMessage(&msg);
		}
	};

	fImpl->fBridge->onProgressChanged = [this](double p) {
		fImpl->fProgress = p;
		if (fTarget.IsValid()) {
			BMessage msg(B_WEB_PAGE_PROGRESS_CHANGED);
			msg.AddDouble(B_WEB_PROGRESS_FIELD, p);
			fTarget.SendMessage(&msg);
		}
	};

	fImpl->fBridge->onLoadFinished = [this]() {
		fImpl->fLoading = false;
		fImpl->fProgress = 1.0;
		if (fTarget.IsValid()) {
			BMessage msg(B_WEB_PAGE_LOAD_FINISHED);
			fTarget.SendMessage(&msg);
		}
	};

	fImpl->fBridge->onLoadFailed = [this](const std::string& e) {
		fImpl->fLoading = false;
		if (fTarget.IsValid()) {
			BMessage msg(B_WEB_PAGE_LOAD_FAILED);
			msg.AddString(B_WEB_ERROR_FIELD, e.c_str());
			fTarget.SendMessage(&msg);
		}
	};

	fImpl->fSelfMessenger = BMessenger(this);
#endif
}


void
BWebView::DetachedFromWindow()
{
#ifdef HAS_WPE
	// Destroy bridge objects; order matters.
	delete fImpl->fNotifier;
	fImpl->fNotifier = NULL;
	delete fImpl->fEventAdapter;
	fImpl->fEventAdapter = NULL;
	delete fImpl->fBridge;
	fImpl->fBridge = NULL;
	fImpl->fCompositor = NULL;
	fImpl->fSelfMessenger = BMessenger();
#endif

	fImpl->fHostView = NULL;
	BView::DetachedFromWindow();
}


void
BWebView::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);

#ifdef HAS_WPE
	if (fImpl->fBridge && fImpl->fBridge->IsValid())
		fImpl->fBridge->Resize((int)width, (int)height);
#endif
}


void
BWebView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgFrameReady:
			Invalidate();
			break;
		default:
			BView::MessageReceived(message);
			break;
	}
}


void
BWebView::Draw(BRect updateRect)
{
	BView::Draw(updateRect);

#ifdef HAS_WPE
	if (fImpl->fCompositor) {
		BBitmap* front = fImpl->fCompositor->AcquireFront();
		if (front && front->IsValid()) {
			DrawBitmap(front, front->Bounds(),
				Bounds(), B_FILTER_BITMAP_BILINEAR);
		}
		fImpl->fCompositor->ReleaseFront();
		return;
	}
#endif

	// Stub fallback when WPE is not linked.
	SetHighColor(255, 255, 255);
	FillRect(updateRect);
	SetHighColor(0, 0, 0);
	MovePenTo(BPoint(10, 30));
	DrawString("NetPlus web view (stub \xe2\x80\x94 WPE not linked)");
}


void
BWebView::MouseDown(BPoint where)
{
	BView::MouseDown(where);

#ifdef HAS_WPE
	if (fImpl->fEventAdapter && fImpl->fBridge
		&& fImpl->fBridge->IsValid()) {
		BMessage msg(B_MOUSE_DOWN);
		msg.AddPoint("be:view_where", where);
		fImpl->fEventAdapter->TranslateMouseDown(&msg);
	}
#endif
}


void
BWebView::MouseUp(BPoint where)
{
	BView::MouseUp(where);

#ifdef HAS_WPE
	if (fImpl->fEventAdapter && fImpl->fBridge
		&& fImpl->fBridge->IsValid()) {
		BMessage msg(B_MOUSE_UP);
		msg.AddPoint("be:view_where", where);
		fImpl->fEventAdapter->TranslateMouseUp(&msg);
	}
#endif
}


void
BWebView::MouseMoved(BPoint where, uint32 transit,
	const BMessage* message)
{
	BView::MouseMoved(where, transit, message);

#ifdef HAS_WPE
	if (fImpl->fEventAdapter && fImpl->fBridge
		&& fImpl->fBridge->IsValid()) {
		BMessage msg(B_MOUSE_MOVED);
		msg.AddPoint("be:view_where", where);
		msg.AddInt32("be:transit", transit);
		fImpl->fEventAdapter->TranslateMouseMoved(&msg);
	}
#endif
}


void
BWebView::KeyDown(const char* bytes, int32 numBytes)
{
	BView::KeyDown(bytes, numBytes);

#ifdef HAS_WPE
	if (fImpl->fEventAdapter && fImpl->fBridge
		&& fImpl->fBridge->IsValid()) {
		BMessage msg(B_KEY_DOWN);
		msg.AddInt8("byte", bytes[0]);
		if (numBytes > 1)
			msg.AddData("bytes", B_INT8_TYPE, bytes + 1,
				numBytes - 1);
		fImpl->fEventAdapter->TranslateKeyDown(&msg);
	}
#endif
}


void
BWebView::KeyUp(const char* bytes, int32 numBytes)
{
	BView::KeyUp(bytes, numBytes);

#ifdef HAS_WPE
	if (fImpl->fEventAdapter && fImpl->fBridge
		&& fImpl->fBridge->IsValid()) {
		BMessage msg(B_KEY_UP);
		msg.AddInt8("byte", bytes[0]);
		if (numBytes > 1)
			msg.AddData("bytes", B_INT8_TYPE, bytes + 1,
				numBytes - 1);
		fImpl->fEventAdapter->TranslateKeyUp(&msg);
	}
#endif
}


void
BWebView::LoadURL(const char* url)
{
	if (!url)
		return;

#ifdef HAS_WPE
	if (fImpl->fBridge && fImpl->fBridge->IsValid()) {
		fImpl->fLoading = true;
		fImpl->fProgress = 0.0;
		fImpl->fBridge->LoadURL(std::string(url));
		return;
	}
#endif

	// Stub path: simulate load-finished.
	fImpl->fCurrentURL.SetTo(url);
	fImpl->fLoading = true;
	fImpl->fProgress = 0.0;

	if (fTarget.IsValid()) {
		BMessage msg(B_WEB_PAGE_URL_CHANGED);
		msg.AddString(B_WEB_URL_FIELD, url);
		fTarget.SendMessage(&msg);
	}

	fprintf(stderr, "BWebView::LoadURL(\"%s\") \xe2\x80\x94 stub\n", url);

	fImpl->fLoading = false;
	fImpl->fProgress = 1.0;
	fImpl->fTitle.SetTo(url);

	if (fTarget.IsValid()) {
		BMessage titleMsg(B_WEB_PAGE_TITLE_CHANGED);
		titleMsg.AddString(B_WEB_TITLE_FIELD, fImpl->fTitle.String());
		fTarget.SendMessage(&titleMsg);

		BMessage progressMsg(B_WEB_PAGE_PROGRESS_CHANGED);
		progressMsg.AddDouble(B_WEB_PROGRESS_FIELD, fImpl->fProgress);
		fTarget.SendMessage(&progressMsg);

		BMessage finishedMsg(B_WEB_PAGE_LOAD_FINISHED);
		fTarget.SendMessage(&finishedMsg);
	}
}


void
BWebView::LoadHTML(const char* html, const char* baseURL)
{
#ifdef HAS_WPE
	if (fImpl->fBridge && fImpl->fBridge->IsValid()) {
		fImpl->fBridge->LoadHTML(
			std::string(html ? html : ""),
			std::string(baseURL ? baseURL : ""));
		return;
	}
#endif

	fprintf(stderr, "BWebView::LoadHTML \xe2\x80\x94 stub\n");
}


void
BWebView::GoBack()
{
#ifdef HAS_WPE
	if (fImpl->fBridge && fImpl->fBridge->IsValid()) {
		fImpl->fBridge->GoBack();
		return;
	}
#endif

	fprintf(stderr, "BWebView::GoBack \xe2\x80\x94 stub\n");
}


void
BWebView::GoForward()
{
#ifdef HAS_WPE
	if (fImpl->fBridge && fImpl->fBridge->IsValid()) {
		fImpl->fBridge->GoForward();
		return;
	}
#endif

	fprintf(stderr, "BWebView::GoForward \xe2\x80\x94 stub\n");
}


void
BWebView::Reload()
{
#ifdef HAS_WPE
	if (fImpl->fBridge && fImpl->fBridge->IsValid()) {
		fImpl->fBridge->Reload(false);
		return;
	}
#endif

	fprintf(stderr, "BWebView::Reload \xe2\x80\x94 stub\n");
}


void
BWebView::Stop()
{
	fImpl->fLoading = false;

#ifdef HAS_WPE
	if (fImpl->fBridge && fImpl->fBridge->IsValid()) {
		fImpl->fBridge->Stop();
		return;
	}
#endif

	fprintf(stderr, "BWebView::Stop \xe2\x80\x94 stub\n");
}


bool
BWebView::CanGoBack() const
{
#ifdef HAS_WPE
	if (fImpl->fBridge && fImpl->fBridge->IsValid())
		return fImpl->fBridge->CanGoBack();
#endif

	return false;
}


bool
BWebView::CanGoForward() const
{
#ifdef HAS_WPE
	if (fImpl->fBridge && fImpl->fBridge->IsValid())
		return fImpl->fBridge->CanGoForward();
#endif

	return false;
}


const char*
BWebView::CurrentURL() const
{
#ifdef HAS_WPE
	if (fImpl->fBridge && fImpl->fBridge->IsValid()) {
		std::string url = fImpl->fBridge->CurrentURL();
		const_cast<BWebView*>(this)->fImpl->fCurrentURL.SetTo(
			url.c_str());
	}
#endif

	return fImpl->fCurrentURL.String();
}


const char*
BWebView::Title() const
{
#ifdef HAS_WPE
	if (fImpl->fBridge && fImpl->fBridge->IsValid()) {
		std::string title = fImpl->fBridge->Title();
		const_cast<BWebView*>(this)->fImpl->fTitle.SetTo(
			title.c_str());
	}
#endif

	return fImpl->fTitle.String();
}


double
BWebView::LoadProgress() const
{
#ifdef HAS_WPE
	if (fImpl->fBridge && fImpl->fBridge->IsValid())
		return fImpl->fBridge->LoadProgress();
#endif

	return fImpl->fProgress;
}


bool
BWebView::IsLoading() const
{
#ifdef HAS_WPE
	if (fImpl->fBridge && fImpl->fBridge->IsValid())
		return fImpl->fBridge->IsLoading();
#endif

	return fImpl->fLoading;
}


void
BWebView::SetTarget(const BMessenger& target)
{
	fTarget = target;
}
