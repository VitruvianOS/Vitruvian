/*
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * Distributed under the terms of the LGPL License.
 */
#ifndef WEB_VIEW_H
#define WEB_VIEW_H


#include <View.h>
#include <Messenger.h>
#include <String.h>


class BWebView : public BView {
public:
								BWebView(BRect frame, const char* name);
	virtual						~BWebView();

	virtual void				AttachedToWindow();
	virtual void				DetachedFromWindow();
	virtual void				FrameResized(float width, float height);
	virtual void				Draw(BRect updateRect);
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

	// Messaging
			void				SetTarget(const BMessenger& target);

private:
			struct				WebViewImpl;
			WebViewImpl*		fImpl;
			BMessenger			fTarget;
};


#endif // WEB_VIEW_H
