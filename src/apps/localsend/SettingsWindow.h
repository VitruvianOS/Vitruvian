/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */
#ifndef _SETTINGS_WINDOW_H
#define _SETTINGS_WINDOW_H

#include <String.h>
#include <Window.h>

#include "LsDevice.h"

class BButton;
class BCheckBox;
class BTextControl;
class BTextView;
class LocalSendSettings;

// Settings: device alias, destination folder, PIN requirement, and
// the transfer history list.
class SettingsWindow : public BWindow {
public:
						SettingsWindow(BRect frame,
							LocalSendSettings* settings,
							const LocalSend::LsDevice& device);

	virtual	void		MessageReceived(BMessage* message);

private:
			void		_Save();
			void		_RefreshHistory();

			LocalSendSettings*	fSettings;
			LocalSend::LsDevice	fDevice;
			BTextControl*	fAliasControl;
			BTextControl*	fDestinationControl;
			BCheckBox*		fPinCheck;
			BTextControl*	fPinControl;
			BTextView*		fHistoryView;
};

#endif // _SETTINGS_WINDOW_H
