/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */
#ifndef EXTENSIONS_WINDOW_H
#define EXTENSIONS_WINDOW_H


#include <Window.h>


class BButton;
class BFilePanel;
class BScrollView;
class BView;
struct WebExtensionInfo;


/*!	NetPlus extensions manager: one row per installed extension
	with its enable toggle, remove for user installs and install
	from a folder. The registry itself lives in the app; this
	window only edits it.
*/
class ExtensionsWindow : public BWindow {
public:
								ExtensionsWindow(BRect frame);
	virtual						~ExtensionsWindow();

	virtual void				MessageReceived(BMessage* message);
	virtual bool				QuitRequested();

	// Creates the window on first use and shows or activates it.
	static	void				ShowWindow(BRect frame);

private:
			void				_RebuildList();
			void				_AddRow(const WebExtensionInfo& info);

			BScrollView*		fScroll;
			BView*				fList;
			BButton*			fInstallButton;
			BFilePanel*			fInstallPanel;
};


#endif // EXTENSIONS_WINDOW_H
