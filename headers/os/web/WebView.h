/*
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * Distributed under the terms of the LGPL License.
 */
#ifndef _WEB_VIEW_H
#define _WEB_VIEW_H


#include <View.h>
#include <Messenger.h>
#include <String.h>


class BWebView : public BView {
public:
								BWebView(BRect frame, const char* name,
									uint32 resizingMode = B_FOLLOW_ALL_SIDES,
									uint32 flags = B_WILL_DRAW | B_FRAME_EVENTS
										| B_FULL_UPDATE_ON_RESIZE);
	virtual						~BWebView();

			// Navigation
			void				LoadURL(const char* url);
			void				LoadHTML(const char* html,
									const char* baseURL = nullptr);
			void				Reload();
			void				Stop();
			void				GoBack();
			void				GoForward();
			bool				CanGoBack() const;
			bool				CanGoForward() const;

			// Target for notifications
			void				SetTarget(const BMessenger& messenger);

			// State
	// Navigation state
			bool				CanGoBack() const;
			bool				CanGoForward() const;

	// State queries
			const char*			CurrentURL() const;
			const char*			Title() const;
			float				LoadProgress() const;
			bool				IsLoading() const;

			// JavaScript
			void				EvaluateJS(const char* script);

	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				FrameResized(float width, float height);
	virtual	void				MessageReceived(BMessage* message);

private:
			class				Impl;
			Impl*				fImpl;
			BMessenger			fTarget;
};


#endif /* _WEB_VIEW_H */
