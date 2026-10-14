/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "PhoneSignalView.h"

#include <Application.h>
#include <Catalog.h>
#include <PopUpMenu.h>
#include <MenuItem.h>
#include <Message.h>
#include <MessageRunner.h>
#include <Roster.h>
#include <Window.h>

#include <stdio.h>

#include "NMBackend.h"
#include <telephony/Modem.h>
#include <telephony/TelephonyRoster.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PhoneSignal"


const char* PhoneSignalView::kAppSignature
	= "application/x-vnd.vos-PhoneSignal";
const char* PhoneSignalView::kDeskbarItemName = "PhoneSignal";


enum {
	kMsgRefresh		= 'psRF',
	kMsgNMDevices	= 'psND',
	kMsgMobileProfile	= 'psMP',
	kMsgDeviceStatus	= 'psDS',
	kMsgNMOpDone		= 'psNO',
	kMsgToggleData		= 'psTD',
	kMsgToggleAirplane	= 'psTA',
	kMsgOpenNetwork		= 'psON'
};


static const uint32 kRefreshInterval = 5 * 1000 * 1000;


PhoneSignalView::PhoneSignalView(BRect frame, int32 resizingMode,
	bool inDeskbar)
	:
	BView(frame, kDeskbarItemName, resizingMode,
		B_WILL_DRAW | B_TRANSPARENT_BACKGROUND | B_FRAME_EVENTS),
	fRoster(NULL),
	fInDeskbar(inDeskbar),
	fBars(0),
	fTechLabel(""),
	fNoModem(true),
	fNoService(true),
	fSimLocked(false),
	fAirplane(false),
	fDataConnected(false),
	fRunner(NULL)
{
	_Init();
}


PhoneSignalView::PhoneSignalView(BMessage* archive)
	:
	BView(archive),
	fRoster(NULL),
	fInDeskbar(false),
	fBars(0),
	fTechLabel(""),
	fNoModem(true),
	fNoService(true),
	fSimLocked(false),
	fAirplane(false),
	fDataConnected(false),
	fRunner(NULL)
{
	// Through instantiate_deskbar_item(), so tray mode has to be
	// recovered from who is hosting us.
	app_info info;
	if (be_app->GetAppInfo(&info) == B_OK
			&& !strcasecmp(info.signature, "application/x-vnd.Be-TSKB"))
		fInDeskbar = true;

	_Init();
}


PhoneSignalView::~PhoneSignalView()
{
}


void
PhoneSignalView::_Init()
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	fRoster = BTelephonyRoster::Default();
}


BArchivable*
PhoneSignalView::Instantiate(BMessage* archive)
{
	if (!archive->HasString("add_on"))
		return NULL;
	return new PhoneSignalView(archive);
}


status_t
PhoneSignalView::Archive(BMessage* archive, bool deep) const
{
	BView::Archive(archive, deep);
	archive->AddString("add_on", kAppSignature);
	archive->AddString("class", "PhoneSignalView");
	return B_OK;
}


void
PhoneSignalView::AttachedToWindow()
{
	BMessenger self(this);
	fRoster->StartWatching(self, kTelephonyWatchModems);

	NMBackend* nm = NMBackend::Instance();
	nm->GetDevicesAsync(self, kMsgNMDevices);

	_SyncFromRoster();
	_ScheduleRefresh();
	ResizeTo(Bounds().Width(), fInDeskbar ? Bounds().Height() : 36);
}


void
PhoneSignalView::DetachedFromWindow()
{
	BMessenger self(this);
	fRoster->StopWatching(self);

	delete fRunner;
	fRunner = NULL;
}


void
PhoneSignalView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kTelephonyMsgModemChanged:
		case kTelephonyMsgModemAdded:
		case kTelephonyMsgModemRemoved:
		case kTelephonyMsgServiceAppeared:
		case kTelephonyMsgServiceVanished:
			_SyncFromRoster();
			break;
		case kMsgRefresh:
			_SyncFromRoster();
			break;
		case kMsgNMDevices:
		{
			// Find the GSM modem device once, then read its state
			// and saved profile on every refresh.
			if (fDevicePath.IsEmpty()) {
				int32 count = 0;
				message->FindInt32(kNMFieldDeviceCount, &count);
				for (int32 i = 0; i < count; i++) {
					char name[32];
					snprintf(name, sizeof(name),
						"device_%" B_PRId32, i);
					BMessage info;
					if (message->FindMessage(name, &info)
							!= B_OK)
						continue;
					bool gsm = false;
					info.FindBool(kNMFieldModemIsGSM, &gsm);
					if (!gsm)
						continue;
					info.FindString(kNMFieldPath, &fDevicePath);
					break;
				}
			}
			_RefreshDeviceState();
			break;
		}
		case kMsgMobileProfile:
		{
			message->FindString(kNMFieldMobileName, &fProfileName);
			message->FindString(kNMFieldMobileAPN, &fProfileAPN);
			message->FindString(kNMFieldMobileUser, &fProfileUser);
			message->FindString(kNMFieldMobilePassword,
				&fProfilePassword);
			message->FindString(kNMFieldMobileNumber,
				&fProfileNumber);
			break;
		}
		case kMsgDeviceStatus:
		{
			bool connected = false;
			message->FindBool(kNMFieldMobileConnected, &connected);
			fDataConnected = connected;
			break;
		}
		case kMsgNMOpDone:
			// Connect/disconnect outcome: re-read the status so the
			// menu check mark follows reality, never the request.
			_RefreshDeviceState();
			break;
		case kMsgToggleData:
			_ToggleData();
			break;
		case kMsgToggleAirplane:
			_ToggleAirplane();
			break;
		case kMsgOpenNetwork:
			_OpenNetworkPreflet();
			break;
		default:
			BView::MessageReceived(message);
			break;
	}
	Invalidate();
}


void
PhoneSignalView::_ScheduleRefresh()
{
	delete fRunner;
	BMessenger self(this);
	fRunner = new BMessageRunner(self, new BMessage(kMsgRefresh),
		kRefreshInterval);
}


void
PhoneSignalView::_SyncFromRoster()
{
	fNoService = !fRoster->IsServiceAvailable();

	BModem* modem = fRoster->ModemAt(0);
	if (modem == NULL) {
		fNoModem = true;
		fBars = 0;
		fTechLabel = "";
		fSimLocked = false;
		fAirplane = false;
		Invalidate();
		return;
	}

	fNoModem = false;
	fBars = modem->SignalBars();
	fTechLabel = modem->AccessTypeString();
	fSimLocked = modem->IsSimLocked();
	fAirplane = !modem->IsEnabled();
	Invalidate();
}


void
PhoneSignalView::_RefreshDeviceState()
{
	// Without a modem device there is nothing to poll; the attach and
	// menu paths request the device list themselves.
	if (fDevicePath.IsEmpty())
		return;

	NMBackend* nm = NMBackend::Instance();
	BMessenger self(this);
	nm->GetDeviceInfoAsync(fDevicePath.String(), self, kMsgDeviceStatus);
	nm->GetMobileConnectionAsync(fDevicePath.String(), self,
		kMsgMobileProfile);
}


void
PhoneSignalView::Draw(BRect updateRect)
{
	(void)updateRect;

	if (fNoService || fNoModem) {
		// Airplane or no modem at all: a crossed-out antenna.
		SetPenSize(2);
		SetHighColor(120, 120, 120);
		StrokeEllipse(BPoint(8, 6), Bounds().Width() / 2 - 4,
			Bounds().Height() / 2 - 4);
		StrokeLine(BPoint(4, Bounds().Height() - 4),
			BPoint(Bounds().Width() - 4, 4));
		return;
	}

	// Four bars, left to right, then the technology label.
	float width = Bounds().Width();
	float height = Bounds().Height();
	float barWidth = 3;
	float gap = 2;
	float x = 3;

	SetHighColor(0, 0, 0);
	SetLowColor(ViewColor());

	for (uint32 i = 0; i < 4; i++) {
		float barHeight = (height - 8) * (i + 1) / 4.0;
		BRect bar(x, height - 4 - barHeight, x + barWidth - 1,
			height - 4);
		if (i < fBars)
			FillRect(bar);
		else {
			StrokeRect(bar);
		}
		x += barWidth + gap;
	}

	if (fSimLocked) {
		DrawString("PIN", BPoint(x + 2, height - 6));
		return;
	}

	if (!fTechLabel.IsEmpty() && !fInDeskbar)
		DrawString(fTechLabel.String(), BPoint(x + 3, height - 6));
	else if (fInDeskbar && !fTechLabel.IsEmpty()) {
		// Deskbar tray: label too, the tray is wide enough these days.
		DrawString(fTechLabel.String(), BPoint(x + 3, height - 6));
	}
}


void
PhoneSignalView::MouseDown(BPoint where)
{
	_ShowMenu(where);
}


void
PhoneSignalView::_ShowMenu(BPoint where)
{
	BPopUpMenu* menu = new BPopUpMenu(B_EMPTY_STRING, true, false);

	BMenuItem* dataItem = new BMenuItem(B_TRANSLATE("Mobile data"),
		new BMessage(kMsgToggleData));
	if (fDataConnected)
		dataItem->SetMarked(true);
	menu->AddItem(dataItem);

	BMenuItem* airplaneItem = new BMenuItem(B_TRANSLATE("Airplane mode"),
		new BMessage(kMsgToggleAirplane));
	if (fAirplane)
		airplaneItem->SetMarked(true);
	menu->AddItem(airplaneItem);

	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(B_TRANSLATE("Open Network"),
		new BMessage(kMsgOpenNetwork)));

	ConvertToScreen(&where);
	menu->SetTargetForItems(this);
	menu->Go(where, true, true, true, false);
}


void
PhoneSignalView::_ToggleData()
{
	NMBackend* nm = NMBackend::Instance();
	BMessenger self(this);

	if (fDevicePath.IsEmpty()) {
		nm->GetDevicesAsync(self, kMsgNMDevices);
		return;
	}

	if (fDataConnected) {
		nm->DisconnectMobileAsync(fDevicePath.String(), self,
			kMsgNMOpDone);
		return;
	}

	nm->ConnectMobileAsync(fDevicePath.String(),
		fProfileName.IsEmpty() ? "mobile" : fProfileName.String(),
		fProfileAPN.String(), fProfileUser.String(),
		fProfilePassword.String(), fProfileNumber.String(), true,
		self, kMsgNMOpDone);
}


void
PhoneSignalView::_ToggleAirplane()
{
	BModem* modem = fRoster->ModemAt(0);
	if (modem == NULL)
		return;

	modem->SetEnabled(fAirplane, BMessenger(this), kMsgNMOpDone);
}


void
PhoneSignalView::_OpenNetworkPreflet()
{
	be_roster->Launch("application/x-vnd.vos-Network");
}


// #pragma mark - Replicant entry points


extern "C" _EXPORT BView*
instantiate_deskbar_item(float maxWidth, float maxHeight)
{
	// Square, driven by height: a replicant wider than one row makes
	// TReplicantTray::LocationForReplicant() loop forever.
	(void)maxWidth;
	return new PhoneSignalView(BRect(0, 0, maxHeight - 1, maxHeight - 1),
		B_FOLLOW_LEFT | B_FOLLOW_TOP, true);
}


extern "C" _EXPORT status_t
instantiate_deskbar_item_for_width(float* width, float* height)
{
	*width = 40;
	*height = 24;
	return B_OK;
}
