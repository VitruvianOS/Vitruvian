/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */
#ifndef _LOCAL_SEND_WINDOW_H
#define _LOCAL_SEND_WINDOW_H

#include <Window.h>

#include <vector>

class BButton;
class BFilePanel;
class BListView;
class BStatusBar;
class BStringView;
class LocalSendManager;
class LocalSendSettings;

class LocalSendWindow : public BWindow {
public:
						LocalSendWindow(BRect frame,
							LocalSendManager* manager,
							LocalSendSettings* settings);

	virtual	void		MessageReceived(BMessage* message);
	virtual	bool		QuitRequested();

private:
			void		_HandleDeviceUpdate(BMessage* message);
			void		_HandleIncomingPrompt(BMessage* message);
			void		_HandleProgress(BMessage* message);
			void		_HandleDone(BMessage* message);
			void		_SendToSelected();
			void		_PickFiles();
			void		_ShowSettings();
			void		_UpdateStatus(const BString& text);

			LocalSendManager*	fManager;
			LocalSendSettings*	fSettings;
			BListView*		fDeviceList;
			BStatusBar*		fProgress;
			BStringView*	fStatus;
			BButton*		fSendButton;
			BFilePanel*		fFilePanel;
			std::vector<BString>	fPendingFiles;
			float			fLastFraction;
};

#endif // _LOCAL_SEND_WINDOW_H
