/*
 * Copyright 2026, VitruvianOS Project.
 * Distributed under the terms of the LGPL License.
 */
#ifndef TOOLBAR_VIEW_H
#define TOOLBAR_VIEW_H


#include <Button.h>
#include <TextControl.h>
#include <View.h>


class ToolbarView : public BView {
public:
								ToolbarView(BRect frame);
	virtual						~ToolbarView();

	virtual void				AttachedToWindow();
	virtual void				MessageReceived(BMessage* message);

			void				SetURL(const char* url);
			void				SetLoading(bool loading);
			void				SetCanGoBack(bool can);
			void				SetCanGoForward(bool can);

private:
			BButton*			fBackButton;
			BButton*			fForwardButton;
			BButton*			fReloadStopButton;
			BTextControl*		fURLBar;
			bool				fLoading;
};


#endif // TOOLBAR_VIEW_H
