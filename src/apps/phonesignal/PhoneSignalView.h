/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _PHONE_SIGNAL_VIEW_H
#define _PHONE_SIGNAL_VIEW_H


#include <View.h>
#include <Messenger.h>
#include <String.h>


class BPopUpMenu;
class BTelephonyRoster;


// Deskbar/top-bar replicant: the classic phone signal bars (0-4 from
// SignalQuality) plus the technology label, with an airplane / no-SIM
// state and a click menu for mobile data, airplane mode and opening
// the Network preflet.
class PhoneSignalView : public BView {
public:
								PhoneSignalView(BRect frame,
									int32 resizingMode,
									bool inDeskbar = false);
								PhoneSignalView(BMessage* archive);
	virtual						~PhoneSignalView();

	static	BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* archive,
									bool deep = true) const;

	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				GetPreferredSize(float* _width,
									float* _height);

	static	const char*			kAppSignature;
	static	const char*			kDeskbarItemName;

	private:
			void				_Init();
			void				_SyncFromRoster();
			void				_RefreshDeviceState();
			void				_ShowMenu(BPoint where);
			void				_ToggleData();
			void				_ToggleAirplane();
			void				_OpenNetworkPreflet();
			void				_Quit();
			void				_ScheduleRefresh();
			void				_StatusStrings(BString& title,
									BString& detail);
			void				_FitWindow();
			void				_DrawTrayIcon(BRect bounds);
			void				_DrawPanel(BRect bounds);
			bool				_RadioOff() const
									{ return fNoModem || fAirplane; }

			BTelephonyRoster*	fRoster;
			bool				fInDeskbar;
			bool				fReplicated;
			uint32				fBars;
			BString				fTechLabel;
			BString				fOperator;
			bool				fNoModem;
			bool				fNoService;
			bool				fSimLocked;
			bool				fAirplane;
			bool				fDataConnected;
			BString				fDevicePath;
			BString				fProfileName;
			BString				fProfileAPN;
			BString				fProfileUser;
			BString				fProfilePassword;
			BString				fProfileNumber;
			class BMessageRunner* fRunner;
};


#endif // _PHONE_SIGNAL_VIEW_H
