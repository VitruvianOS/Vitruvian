/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef MOBILE_BROADBAND_VIEW_H
#define MOBILE_BROADBAND_VIEW_H


#include <String.h>
#include <View.h>


class BButton;
class BCheckBox;
class BStringView;
class BTextControl;


// Mobile broadband pane: NM modem device status plus APN/user/password
// form. Signal, operator and SIM PIN come from ModemManagerBackend;
// connection create/connect/disconnect from NMBackend.
class MobileBroadbandView : public BView {
public:
	static const uint32 kMsgDirtyChanged = 'mDty';
	static const uint32 kMsgRefreshMM = 'mRfr';

							MobileBroadbandView();
	virtual					~MobileBroadbandView();

	virtual	void			AttachedToWindow();
	virtual	void			MessageReceived(BMessage* message);

			// Takes the NM device snapshot for a modem device, or empties
			// the pane when the modem went away.
			void			SetModemDevice(const BMessage& deviceInfo);
			void			ClearModem();

			bool			IsDirty() const;
			void			Revert();

private:
			void			_SetStatusText(const BString& text);
			void			_SetSignalText(const BString& text);
			void			_SetOperatorText(const BString& text);
			void			_SetPINVisible(bool visible);
			void			_EnableForm(bool enable);
			void			_FillFormFromProfile(BMessage& profile);
			void			_RequestProfile();
			void			_RequestMMStatus();
			void			_OnProfileReady(BMessage& message);
			void			_OnMMStatusReady(BMessage& message);
			void			_OnConnectDone(BMessage& message);
			void			_OnDisconnectDone(BMessage& message);
			void			_OnUnlockDone(BMessage& message);
			void			_ShowError(BMessage& message);

			BString			fDevicePath;
			BString			fMMModemPath;
			bool			fIsGSM;
			bool			fConnected;
			bool			fHasProfile;

			BStringView*	fStatusView;
			BStringView*	fSignalView;
			BStringView*	fOperatorView;
			BTextControl*	fAPNField;
			BTextControl*	fUserField;
			BTextControl*	fPasswordField;
			BTextControl*	fNumberField;
			BTextControl*	fPINField;
			BButton*		fConnectButton;
			BButton*		fDisconnectButton;
			BButton*		fUnlockButton;
			BButton*		fRefreshButton;

			uint32			fSnapshotAPNHash;
			uint32			fSnapshotUserHash;
			uint32			fSnapshotNumberHash;
};


#endif // MOBILE_BROADBAND_VIEW_H
