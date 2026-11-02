/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _DIALER_VIEW_H
#define _DIALER_VIEW_H


#include <View.h>


class BButton;
class BGridLayout;
class BListView;
class BMenuField;
class BPopUpMenu;
class BScrollView;
class BStringView;
class BTelephonyRoster;
class BTextControl;


enum {
	kMsgDial			= 'phDL',
	kMsgDialReply		= 'phDR',
	kMsgHangUpAll		= 'phHU',
	kMsgOpReply			= 'phOP',
	kMsgKeypadKey		= 'phKY',
	kMsgLineSelected	= 'phLN',
	kMsgDialerMenu		= 'phMN',
	kMsgToggleCallLog	= 'phTL'
};


class DialerView : public BView {
public:
								DialerView(const char* name);
								DialerView(BMessage* archive);

	virtual						~DialerView();

	static	BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* archive,
									bool deep = true) const;

	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				MessageReceived(BMessage* message);

			void				AppendKeypadKey(char key);
			void				RefreshLines();

			const char*			SelectedLine();

private:
			void				_Build();
			void				_Dial();
			void				_HangUpAll();
			void				_UpdateStatus();
			void				_SeedCallLog();
			void				_ShowMenu();
			bool				_IsReplicated() const;

	static const char*			kAppSignature;

			BTelephonyRoster*	fRoster;
			BTextControl*		fNumberControl;
			BStringView*		fStatusView;
			BButton*			fMenuButton;
			BListView*			fLogView;
			BScrollView*		fLogScroll;
			BButton*			fDialButton;
			BButton*			fHangUpButton;
			BGridLayout*		fKeypad;
			BPopUpMenu*			fLineMenu;
};


#endif // _DIALER_VIEW_H
