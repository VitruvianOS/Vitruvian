/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef SETTINGS_WINDOW_H
#define SETTINGS_WINDOW_H


#include <Window.h>


class BButton;
class BCheckBox;
class BFilePanel;
class BMenuField;
class BTextControl;


class SettingsWindow : public BWindow {
public:
								SettingsWindow(BRect frame);
	virtual						~SettingsWindow();

	virtual void				MessageReceived(BMessage* message);
	virtual bool				QuitRequested();

	// Creates the window on first use and shows or activates it.
	static	void				ShowWindow(BRect frame);

private:
			void				_LoadFromSettings();
			void				_ApplyAndSave();
			void				_UpdateCustomTemplateEnabled();

			BTextControl*		fHomepage;
			BMenuField*			fSearchEngine;
			BTextControl*		fCustomTemplate;
			BTextControl*		fDownloadDir;
			BButton*			fBrowseButton;
			BCheckBox*			fAskDownload;
			BCheckBox*			fJavaScript;
			BCheckBox*			fImages;
			BTextControl*		fUserAgentSuffix;
			BTextControl*		fBlockedDomains;
			BCheckBox*			fPrivateBrowsing;
			BTextControl*		fZoomDefault;
			BMenuField*			fAutoplay;
			BCheckBox*			fRestoreSession;
			BButton*			fClearDataButton;
			BFilePanel*			fBrowsePanel;
};


#endif // SETTINGS_WINDOW_H
