/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "MobileBroadbandView.h"

#include "ModemManagerBackend.h"
#include "NMBackend.h"

#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <Messenger.h>
#include <StringView.h>
#include <TextControl.h>
#include <Window.h>

#include <stdio.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MobileBroadbandView"


static const uint32 kMsgFieldModified = 'mFld';
static const uint32 kMsgConnect = 'mCnc';
static const uint32 kMsgDisconnect = 'mDsc';
static const uint32 kMsgUnlock = 'mUnl';
static const uint32 kMsgConnectDone = 'mCdn';
static const uint32 kMsgDisconnectDone = 'mDdn';
static const uint32 kMsgProfileReady = 'mPfl';
static const uint32 kMsgMMStatusReady = 'mMmr';
static const uint32 kMsgUnlockDone = 'mUdn';


MobileBroadbandView::MobileBroadbandView()
	:
	BView("mobileBroadbandView", B_WILL_DRAW),
	fIsGSM(true),
	fConnected(false),
	fHasProfile(false),
	fStatusView(NULL),
	fSignalView(NULL),
	fOperatorView(NULL),
	fAPNField(NULL),
	fUserField(NULL),
	fPasswordField(NULL),
	fNumberField(NULL),
	fPINField(NULL),
	fConnectButton(NULL),
	fDisconnectButton(NULL),
	fUnlockButton(NULL),
	fRefreshButton(NULL),
	fSnapshotAPNHash(0),
	fSnapshotUserHash(0),
	fSnapshotNumberHash(0)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	fStatusView = new BStringView("status", B_TRANSLATE("Disconnected"));
	fSignalView = new BStringView("signal", B_TRANSLATE("Signal: unknown"));
	fOperatorView = new BStringView("operator",
		B_TRANSLATE("Operator: unknown"));

	fAPNField = new BTextControl("apn", B_TRANSLATE("APN:"),
		"", new BMessage(kMsgFieldModified));
	fUserField = new BTextControl("user", B_TRANSLATE("Username:"),
		"", new BMessage(kMsgFieldModified));
	fPasswordField = new BTextControl("password", B_TRANSLATE("Password:"),
		"", new BMessage(kMsgFieldModified));
	fNumberField = new BTextControl("number", B_TRANSLATE("Number:"),
		"", new BMessage(kMsgFieldModified));
	fPINField = new BTextControl("pin", B_TRANSLATE("SIM PIN:"),
		"", new BMessage(kMsgFieldModified));
	fPINField->Hide();

	fConnectButton = new BButton("connect", B_TRANSLATE("Connect"),
		new BMessage(kMsgConnect));
	fDisconnectButton = new BButton("disconnect", B_TRANSLATE("Disconnect"),
		new BMessage(kMsgDisconnect));
	fUnlockButton = new BButton("unlock", B_TRANSLATE("Unlock SIM"),
		new BMessage(kMsgUnlock));
	fRefreshButton = new BButton("refresh", B_TRANSLATE("Refresh"),
		new BMessage(kMsgRefreshMM));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.Add(fStatusView)
		.Add(fSignalView)
		.Add(fOperatorView)
		.AddGrid()
			.Add(fAPNField, 0, 0, 2, 1)
			.Add(fUserField, 0, 1, 2, 1)
			.Add(fPasswordField, 0, 2, 2, 1)
			.Add(fNumberField, 0, 3, 2, 1)
			.Add(fPINField, 0, 4, 2, 1)
		.End()
		.AddGroup(B_HORIZONTAL)
			.Add(fConnectButton)
			.Add(fDisconnectButton)
			.Add(fUnlockButton)
			.AddGlue()
			.Add(fRefreshButton)
		.End()
		.AddGlue();

	fAPNField->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fUserField->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fPasswordField->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fNumberField->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fPINField->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	_EnableForm(false);
}


MobileBroadbandView::~MobileBroadbandView()
{
}


void
MobileBroadbandView::AttachedToWindow()
{
	BView::AttachedToWindow();

	NMBackend* nm = NMBackend::Instance();
	if (nm != NULL) {
		nm->StartWatching(BMessenger(this),
			NMBackend::NOTIFICATION_DEVICE_STATE_CHANGED
				| NMBackend::NOTIFICATION_CONNECTION_STATUS_CHANGED);
	}

	ModemManagerBackend* mm = ModemManagerBackend::Instance();
	if (mm != NULL) {
		mm->StartWatching(BMessenger(this),
			ModemManagerBackend::NOTIFICATION_MODEM_SIGNAL_CHANGED
				| ModemManagerBackend::NOTIFICATION_MODEM_STATE_CHANGED);
	}
}


void
MobileBroadbandView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgFieldModified:
		{
			Window()->PostMessage(kMsgDirtyChanged);
			_EnableForm(!fDevicePath.IsEmpty());
			break;
		}

		case kMsgRefreshMM:
			_RequestMMStatus();
			break;

		case kMsgConnect:
		{
			NMBackend* nm = NMBackend::Instance();
			if (nm == NULL || fDevicePath.IsEmpty())
				break;

			const char* name = fAPNField->Text();
			if (name == NULL || name[0] == '\0')
				name = "mobile";

			nm->ConnectMobileAsync(fDevicePath.String(), name,
				fAPNField->Text(), fUserField->Text(),
				fPasswordField->Text(), fNumberField->Text(), true,
				BMessenger(this), kMsgConnectDone);
			_EnableForm(false);
			break;
		}

		case kMsgDisconnect:
		{
			NMBackend* nm = NMBackend::Instance();
			if (nm == NULL || fDevicePath.IsEmpty())
				break;

			nm->DisconnectMobileAsync(fDevicePath.String(),
				BMessenger(this), kMsgDisconnectDone);
			break;
		}

		case kMsgUnlock:
		{
			ModemManagerBackend* mm = ModemManagerBackend::Instance();
			const char* pin = fPINField->Text();
			if (mm == NULL || fMMModemPath.IsEmpty() || pin == NULL
					|| pin[0] == '\0')
				break;

			mm->UnlockSimAsync(fMMModemPath.String(), pin,
				BMessenger(this), kMsgUnlockDone);
			break;
		}

		case kMsgProfileReady:
			_OnProfileReady(*message);
			break;

		case kMsgMMStatusReady:
			_OnMMStatusReady(*message);
			break;

		case kMsgConnectDone:
			_OnConnectDone(*message);
			break;

		case kMsgDisconnectDone:
			_OnDisconnectDone(*message);
			break;

		case kMsgUnlockDone:
			_OnUnlockDone(*message);
			break;

		case NMBackend::NOTIFICATION_DEVICE_STATE_CHANGED:
		case NMBackend::NOTIFICATION_CONNECTION_STATUS_CHANGED:
			if (!fDevicePath.IsEmpty())
				_RequestProfile();
			break;

		case ModemManagerBackend::NOTIFICATION_MODEM_SIGNAL_CHANGED:
		case ModemManagerBackend::NOTIFICATION_MODEM_STATE_CHANGED:
			_RequestMMStatus();
			break;

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
MobileBroadbandView::SetModemDevice(const BMessage& deviceInfo)
{
	const char* path = NULL;
	if (deviceInfo.FindString(kNMFieldPath, &path) != B_OK)
		return;

	fDevicePath = path;
	deviceInfo.FindBool(kNMFieldModemIsGSM, &fIsGSM);
	deviceInfo.FindBool(kNMFieldMobileConnected, &fConnected);

	_EnableForm(true);
	fStatusView->SetText(fConnected
		? B_TRANSLATE("Connected") : B_TRANSLATE("Disconnected"));

	_RequestProfile();
	_RequestMMStatus();
}


void
MobileBroadbandView::ClearModem()
{
	fDevicePath = "";
	fMMModemPath = "";
	fConnected = false;
	fHasProfile = false;
	_SetStatusText(B_TRANSLATE("No modem"));
	_SetSignalText(B_TRANSLATE("Signal: unknown"));
	_SetOperatorText(B_TRANSLATE("Operator: unknown"));
	_EnableForm(false);
}


bool
MobileBroadbandView::IsDirty() const
{
	// Dirty only after a profile was loaded and a credential field changed.
	if (!fHasProfile)
		return false;

	if (fAPNField->Text() != NULL
			&& BString(fAPNField->Text()).HashValue() != fSnapshotAPNHash)
		return true;
	if (fUserField->Text() != NULL
			&& BString(fUserField->Text()).HashValue() != fSnapshotUserHash)
		return true;
	if (fNumberField->Text() != NULL
			&& BString(fNumberField->Text()).HashValue()
				!= fSnapshotNumberHash)
		return true;
	return false;
}


void
MobileBroadbandView::Revert()
{
	_RequestProfile();
}


void
MobileBroadbandView::_SetStatusText(const BString& text)
{
	fStatusView->SetText(text.String());
}


void
MobileBroadbandView::_SetSignalText(const BString& text)
{
	fSignalView->SetText(text.String());
}


void
MobileBroadbandView::_SetOperatorText(const BString& text)
{
	fOperatorView->SetText(text.String());
}


void
MobileBroadbandView::_SetPINVisible(bool visible)
{
	if (visible)
		fPINField->Show();
	else
		fPINField->Hide();
}


void
MobileBroadbandView::_EnableForm(bool enable)
{
	fAPNField->SetEnabled(enable && fIsGSM);
	fUserField->SetEnabled(enable);
	fPasswordField->SetEnabled(enable);
	fNumberField->SetEnabled(enable);
	fConnectButton->SetEnabled(enable && !fConnected);
	fDisconnectButton->SetEnabled(enable && fConnected);
	fRefreshButton->SetEnabled(enable);
}


void
MobileBroadbandView::_FillFormFromProfile(BMessage& profile)
{
	const char* apn = NULL;
	const char* user = NULL;
	const char* password = NULL;
	const char* number = NULL;
	const char* name = NULL;

	profile.FindString(kNMFieldMobileAPN, &apn);
	profile.FindString(kNMFieldMobileUser, &user);
	profile.FindString(kNMFieldMobilePassword, &password);
	profile.FindString(kNMFieldMobileNumber, &number);
	profile.FindString(kNMFieldMobileName, &name);

	fAPNField->SetText(apn != NULL ? apn : "");
	fUserField->SetText(user != NULL ? user : "");
	fPasswordField->SetText(password != NULL ? password : "");
	fNumberField->SetText(number != NULL ? number : "");

	fSnapshotAPNHash = fAPNField->Text() != NULL
		? BString(fAPNField->Text()).HashValue() : 0;
	fSnapshotUserHash = fUserField->Text() != NULL
		? BString(fUserField->Text()).HashValue() : 0;
	fSnapshotNumberHash = fNumberField->Text() != NULL
		? BString(fNumberField->Text()).HashValue() : 0;
}


void
MobileBroadbandView::_RequestProfile()
{
	NMBackend* nm = NMBackend::Instance();
	if (nm == NULL || fDevicePath.IsEmpty())
		return;

	nm->GetMobileConnectionAsync(fDevicePath.String(), BMessenger(this),
		kMsgProfileReady);
}


void
MobileBroadbandView::_RequestMMStatus()
{
	ModemManagerBackend* mm = ModemManagerBackend::Instance();
	if (mm == NULL)
		return;

	BMessage modems;
	if (mm->GetModems(&modems) != B_OK)
		return;

	int32 count = 0;
	modems.FindInt32(kMMFieldModemCount, &count);
	if (count <= 0) {
		_SetSignalText(B_TRANSLATE("Signal: unknown"));
		_SetOperatorText(B_TRANSLATE("Operator: unknown"));
		_SetPINVisible(false);
		return;
	}

	char key[32];
	snprintf(key, sizeof(key), "modem_%" B_PRId32, (int32)0);
	BMessage modemInfo;
	if (modems.FindMessage(key, &modemInfo) != B_OK)
		return;

	_OnMMStatusReady(modemInfo);
}


void
MobileBroadbandView::_OnProfileReady(BMessage& message)
{
	int32 status = B_ERROR;
	message.FindInt32("status", &status);
	if (status != B_OK)
		return;

	bool connected = false;
	message.FindBool(kNMFieldMobileConnected, &connected);
	fConnected = connected;
	message.FindBool(kNMFieldMobileHasProfile, &fHasProfile);

	_SetStatusText(fConnected
		? B_TRANSLATE("Connected") : B_TRANSLATE("Disconnected"));
	_EnableForm(true);

	if (fHasProfile)
		_FillFormFromProfile(message);
}


void
MobileBroadbandView::_OnMMStatusReady(BMessage& message)
{
	const char* mmPath = NULL;
	if (message.FindString(kMMFieldModemPath, &mmPath) == B_OK
			&& mmPath != NULL)
		fMMModemPath = mmPath;

	uint32 quality = 0;
	uint32 bars = 0;
	message.FindUInt32(kMMFieldModemSignalQuality, &quality);
	message.FindUInt32(kMMFieldModemSignalBars, &bars);

	if (message.HasUInt32(kMMFieldModemSignalQuality)) {
		BString signal(B_TRANSLATE("Signal: "));
		signal << quality << "% (" << bars << " bars)";
		_SetSignalText(signal);
	}

	const char* operatorName = NULL;
	if (message.FindString(kMMFieldModemOperatorName, &operatorName) == B_OK
			&& operatorName != NULL && operatorName[0] != '\0') {
		BString text(B_TRANSLATE("Operator: "));
		text << operatorName;
		_SetOperatorText(text);
	}

	bool locked = false;
	bool pinRequired = false;
	bool pukRequired = false;
	uint32 retries = 0;
	message.FindBool(kMMFieldSimLocked, &locked);
	message.FindBool(kMMFieldSimPinRequired, &pinRequired);
	message.FindBool(kMMFieldSimPukRequired, &pukRequired);
	message.FindUInt32(kMMFieldSimUnlockRetries, &retries);

	if (pukRequired) {
		_SetOperatorText(B_TRANSLATE("SIM PUK required"));
		_SetPINVisible(false);
		fUnlockButton->SetEnabled(false);
	} else if (locked && pinRequired) {
		BString text(B_TRANSLATE("SIM PIN required"));
		if (retries > 0)
			text << B_TRANSLATE(", tries left: ") << retries;
		_SetOperatorText(text);
		_SetPINVisible(true);
		fUnlockButton->SetEnabled(!fMMModemPath.IsEmpty());
	} else if (locked) {
		_SetOperatorText(B_TRANSLATE("SIM locked"));
		_SetPINVisible(false);
		fUnlockButton->SetEnabled(false);
	} else {
		_SetPINVisible(false);
		fUnlockButton->SetEnabled(false);
	}
}


void
MobileBroadbandView::_OnConnectDone(BMessage& message)
{
	int32 status = B_ERROR;
	message.FindInt32("status", &status);
	if (status != B_OK)
		_ShowError(message);

	_EnableForm(true);
	_RequestProfile();
	_RequestMMStatus();
}


void
MobileBroadbandView::_OnDisconnectDone(BMessage& message)
{
	int32 status = B_ERROR;
	message.FindInt32("status", &status);
	if (status != B_OK)
		_ShowError(message);

	fConnected = false;
	_SetStatusText(B_TRANSLATE("Disconnected"));
	_EnableForm(true);
}


void
MobileBroadbandView::_OnUnlockDone(BMessage& message)
{
	int32 status = B_ERROR;
	message.FindInt32("status", &status);
	if (status != B_OK)
		_ShowError(message);
	else
		fPINField->SetText("");

	_RequestMMStatus();
	_SetPINVisible(false);
	fUnlockButton->SetEnabled(false);
}


void
MobileBroadbandView::_ShowError(BMessage& message)
{
	BString reason;
	message.FindString("reason", &reason);

	BString text(B_TRANSLATE("Mobile broadband operation failed."));
	if (!reason.IsEmpty())
		text << "\n" << reason;

	BAlert* alert = new BAlert(B_TRANSLATE("Mobile broadband"),
		text.String(), B_TRANSLATE("OK"));
	alert->Go(NULL);
}
