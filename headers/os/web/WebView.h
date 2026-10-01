/*
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * Distributed under the terms of the LGPL License.
 */
#ifndef WEB_VIEW_H
#define WEB_VIEW_H


#include <View.h>
#include <Messenger.h>
#include <String.h>


// Whether media may start playing without a user gesture.
enum web_autoplay_policy {
	B_WEB_AUTOPLAY_ALLOW = 0,
	B_WEB_AUTOPLAY_ALLOW_WITHOUT_SOUND,
	B_WEB_AUTOPLAY_DENY
};


class BWebView : public BView {
public:
								BWebView(BRect frame, const char* name);
	virtual						~BWebView();

	virtual void				AttachedToWindow();
	virtual void				DetachedFromWindow();
	virtual void				FrameResized(float width, float height);
	virtual void				Draw(BRect updateRect);
	virtual void				MessageReceived(BMessage* message);
	virtual void				MakeFocus(bool focused = true);
	virtual void				MouseDown(BPoint where);
	virtual void				MouseUp(BPoint where);
	virtual void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* message);
	virtual void				KeyDown(const char* bytes, int32 numBytes);
	virtual void				KeyUp(const char* bytes, int32 numBytes);

	// Navigation
			void				LoadURL(const char* url);
			void				LoadHTML(const char* html,
									const char* baseURL = nullptr);
			void				GoBack();
			void				GoForward();
			void				Reload();
			void				Stop();

	// Navigation state
			bool				CanGoBack() const;
			bool				CanGoForward() const;

	// State queries
			const char*			CurrentURL() const;
			const char*			Title() const;
			double				LoadProgress() const;
			bool				IsLoading() const;

	// Script message handlers: page scripts post with
	// window.webkit.messageHandlers.<name>.postMessage(...); every
	// message arrives as B_WEB_SCRIPT_MESSAGE on the target set with
	// SetTarget(), carrying B_WEB_HANDLER_FIELD and B_WEB_BODY_FIELD.
			void				AddScriptMessageHandler(const char* name);

	// Runs javascript in the page; the string result of the last
	// expression is posted to replyTarget as replyWhat with
	// B_WEB_SCRIPT_RESULT_FIELD. Asynchronous, never blocks the
	// caller. Ignored while the view is detached.
			void				EvaluateJavaScript(const char* javascript,
									const BMessenger& replyTarget,
									uint32 replyWhat);

	// Messaging
			void				SetTarget(const BMessenger& target);

	// Session: call before the view is shown. The default profile is
	// shared by every view in the process (cookies, local storage);
	// private views get an ephemeral session instead.
			void				SetSession(const char* profileDir,
									bool ephemeral);
	static	void				SetDefaultProfileDir(const char* path);
	static	void				ClearDefaultProfileData();
	// Single app-level sink for the shared profile's download events;
	// with many windows open each event must arrive exactly once.
	static	void				SetDownloadTarget(const BMessenger& target);

	// Preferences: applied immediately, on the GLib thread.
			void				SetBlockedDomains(const char* commaSeparated);
			void				SetJavaScriptEnabled(bool enabled);
			void				SetImagesEnabled(bool enabled);
			void				SetUserAgentSuffix(const char* suffix);
			void				SetZoomLevel(float zoom);
			float				ZoomLevel() const;

	// Fixed when the page view is created: a change reaches views
	// created afterwards, not this one. Call before it is attached.
			void				SetAutoplayPolicy(web_autoplay_policy policy);

	// Downloads: directory and ask-where-to-save apply to the
	// view's (shared per profile) session.
			void				SetDownloadDirectory(const char* path);
			void				SetAskDownloadLocation(bool ask);
			void				CancelDownload(uint64 id);

	// Editing: routed through WebKit and mirrored to BClipboard.
			void				Cut();
			void				Copy();
			void				Paste();
			void				SelectAll();

	// Downloads a URI; a non-empty destination pins the save path.
			void				DownloadURI(const char* uri,
									const char* destination = nullptr);

	// Page scripts and styles: applied to every document the view
	// loads (NetPlus injects the search template for the start page).
			void				AddUserScript(const char* javascript,
									bool atDocumentStart);
			void				AddUserStyleSheet(const char* css);

	// Full-screen video: the window calls this after it entered or
	// left full-screen; the reply tells WebKit the transition done.
			void				NotifyFullScreen(bool entered);

private:
			struct				WebViewImpl;
			WebViewImpl*		fImpl;
			BMessenger			fTarget;
};


#endif // WEB_VIEW_H
