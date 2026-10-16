/*
 * Copyright 2026, VitruvianOS Project.
 * Distributed under the terms of the LGPL License.
 */
#ifndef TOOLBAR_VIEW_H
#define TOOLBAR_VIEW_H


#include <ToolBar.h>
#include <View.h>


class BBitmap;
class BButton;
class BMessage;
class BTextControl;


/*!	Navigation strip rebuilt on the public BToolBar: back, forward,
	reload and home are BToolBar icon actions, so the bar's built-in
	overflow menu collects them when the window narrows. The URL bar
	is a plain BTextControl sibling: BToolBar hosts buttons, so the
	address field stays next to the bar in this view's layout. The
	app-menu (hamburger) button is a BToolBar action at the right
	end, past the glue.
*/
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

			void				FocusURL();

private:
			BToolBar*			fToolBar;
			BTextControl*		fURLBar;
			BBitmap*			fReloadIcon;
			BBitmap*			fStopIcon;
			bool				fLoading;
};


#endif // TOOLBAR_VIEW_H
