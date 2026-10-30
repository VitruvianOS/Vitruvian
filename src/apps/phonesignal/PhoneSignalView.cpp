/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "PhoneSignalView.h"

#include <Application.h>
#include <Catalog.h>
#include <Deskbar.h>
#include <Dragger.h>
#include <Font.h>
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
	kMsgOpenNetwork		= 'psON',
	kMsgQuit		= 'psQT'
};


static const uint32 kRefreshInterval = 5 * 1000 * 1000;


PhoneSignalView::PhoneSignalView(BRect frame, int32 resizingMode,
	bool inDeskbar)
	:
	BView(frame, kDeskbarItemName, resizingMode,
		B_WILL_DRAW | B_TRANSPARENT_BACKGROUND | B_FRAME_EVENTS),
	fRoster(NULL),
	fInDeskbar(inDeskbar),
	fReplicated(false),
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

	if (!inDeskbar) {
		// The dragger is what lets the view be pulled onto the desktop.
		frame.OffsetTo(B_ORIGIN);
		frame.top = frame.bottom - 7;
		frame.left = frame.right - 7;
		AddChild(new BDragger(frame, this,
			B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM));
	}
}


PhoneSignalView::PhoneSignalView(BMessage* archive)
	:
	BView(archive),
	fRoster(NULL),
	fInDeskbar(false),
	fReplicated(false),
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
	else
		fReplicated = true;

	_Init();
}


PhoneSignalView::~PhoneSignalView()
{
}


void
PhoneSignalView::_Init()
{
	// The tray draws over Deskbar rows; the window keeps a panel backdrop.
	SetViewColor(fInDeskbar
		? B_TRANSPARENT_COLOR : ui_color(B_PANEL_BACKGROUND_COLOR));
	SetLowColor(ViewColor());
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
		case kMsgQuit:
			_Quit();
			return;
		default:
			BView::MessageReceived(message);
			break;
	}
	Invalidate();
}


void
PhoneSignalView::_Quit()
{
	if (fInDeskbar) {
		BDeskbar deskbar;
		deskbar.RemoveItem(kDeskbarItemName);
	} else if (fReplicated) {
		BDragger* dragger = dynamic_cast<BDragger*>(ChildAt(0));
		if (dragger != NULL)
			BMessenger(dragger).SendMessage(B_TRASH_TARGET);
	} else
		be_app->PostMessage(B_QUIT_REQUESTED);
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
		fOperator = "";
		fSimLocked = false;
		fAirplane = false;
		_FitWindow();
		Invalidate();
		return;
	}

	fNoModem = false;
	fBars = modem->SignalBars();
	// The access type and operator strings are NULL before registration.
	const char* tech = modem->AccessTypeString();
	fTechLabel = tech != NULL ? tech : "";
	const char* oper = modem->OperatorName();
	fOperator = oper != NULL ? oper : "";
	fSimLocked = modem->IsSimLocked();
	fAirplane = !modem->IsEnabled();
	_FitWindow();
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

	if (fInDeskbar)
		_DrawTrayIcon(Bounds());
	else
		_DrawPanel(Bounds());
}


// Tray rows are square and tiny: four bars, plus a slash when the radio
// cannot report (no modem or airplane), so the state reads at a glance.
void
PhoneSignalView::_DrawTrayIcon(BRect bounds)
{
	float size = bounds.Width() + 1;
	if (size > bounds.Height() + 1)
		size = bounds.Height() + 1;

	BRect icon(bounds.left, bounds.top,
		bounds.left + size - 1, bounds.top + size - 1);
	icon.InsetBy(2, 2);

	const float barWidth = 2;
	const float gap = 2;
	float innerHeight = icon.Height() + 1;
	float bottom = icon.bottom;
	float x = icon.left;

	for (uint32 i = 0; i < 4; i++) {
		float barHeight = innerHeight * (i + 1) / 4.0f;
		BRect bar(x, bottom + 1 - barHeight, x + barWidth - 1, bottom);
		if (!_RadioOff() && !fNoService && i < fBars) {
			SetHighColor(0, 0, 0);
			SetLowColor(ViewColor());
			FillRect(bar);
		} else {
			SetHighColor(160, 160, 160);
			SetLowColor(ViewColor());
			StrokeRect(bar);
		}
		x += barWidth + gap;
	}

	if (_RadioOff()) {
		SetPenSize(2);
		SetHighColor(90, 90, 90);
		StrokeLine(BPoint(icon.left - 1, icon.bottom + 1),
			BPoint(icon.right + 2, icon.top - 1));
		SetPenSize(1);
	}
}


// Shared with sizing so the window can be measured before drawing.
void
PhoneSignalView::_StatusStrings(BString& title, BString& detail)
{
	if (fNoModem) {
		title = B_TRANSLATE("No modem");
		detail = B_TRANSLATE("Connect a modem to show signal");
	} else if (fAirplane) {
		title = B_TRANSLATE("Airplane mode");
		detail = B_TRANSLATE("The radio is switched off");
	} else if (fSimLocked) {
		title = B_TRANSLATE("SIM locked");
		detail = B_TRANSLATE("Enter the SIM PIN to register");
	} else if (fNoService) {
		title = B_TRANSLATE("No service");
		detail = B_TRANSLATE("Searching for a network");
	} else {
		if (fOperator.IsEmpty())
			title = B_TRANSLATE("Network registered");
		else
			title = fOperator;
		char bars[32];
		snprintf(bars, sizeof(bars), B_TRANSLATE("%u/4 bars"),
			(unsigned)fBars);
		if (fTechLabel.IsEmpty()) {
			detail = bars;
		} else {
			detail = fTechLabel;
			detail << " · " << bars;
		}
	}
}


// The window has to hold the meter column, both status lines and the
// dragger at the current font size; the floors keep the established look.
void
PhoneSignalView::GetPreferredSize(float* _width, float* _height)
{
	BString title;
	BString detail;
	_StatusStrings(title, detail);

	const float pad = 16;
	const float barsWidth = 4 * 10 + 3 * 6;
	const float textX = pad + barsWidth + 18;

	font_height plainHeight;
	be_plain_font->GetHeight(&plainHeight);

	float titleWidth = be_bold_font->StringWidth(title.String());
	float detailWidth = be_plain_font->StringWidth(detail.String());
	float width = textX
		+ (titleWidth > detailWidth ? titleWidth : detailWidth) + pad;
	if (width < 324)
		width = 324;

	float baseline1 = pad + plainHeight.ascent;
	float baseline2 = baseline1 + plainHeight.descent + 4
		+ plainHeight.ascent;
	float height = baseline2 + plainHeight.descent + pad;
	if (height < 94)
		height = 94;

	if (_width != NULL)
		*_width = width;
	if (_height != NULL)
		*_height = height;
}


void
PhoneSignalView::_FitWindow()
{
	// Growing only: the deskbar item has a fixed square slot and the
	// window must not shrink away from the established panel size.
	if (fInDeskbar || Window() == NULL)
		return;

	float width;
	float height;
	GetPreferredSize(&width, &height);
	if (width > Bounds().Width() + 0.5 || height > Bounds().Height() + 0.5)
		Window()->ResizeTo(width, height);
}


// Window mode: the meter on the left, status and detail on the right.
// Title carries the operator (or the reason there is none), the second
// line carries access type and bars out of four.
void
PhoneSignalView::_DrawPanel(BRect bounds)
{
	const float pad = 16;
	const float barWidth = 10;
	const float gap = 6;
	const float barsHeight = 44;
	float barsBottom = pad + barsHeight;
	float barsWidth = 4 * barWidth + 3 * gap;
	bool radioOff = _RadioOff();
	bool showBars = !radioOff && !fNoService;

	SetLowColor(ViewColor());
	float x = pad;
	for (uint32 i = 0; i < 4; i++) {
		float barHeight = barsHeight * (i + 1) / 4.0f;
		BRect bar(x, barsBottom + 1 - barHeight, x + barWidth - 1,
			barsBottom);
		if (showBars && i < fBars) {
			SetHighColor(0, 0, 0);
			FillRect(bar);
		} else {
			SetHighColor(160, 160, 160);
			StrokeRect(bar);
		}
		x += barWidth + gap;
	}

	if (radioOff) {
		SetPenSize(2);
		SetHighColor(90, 90, 90);
		StrokeLine(BPoint(pad - 1, barsBottom + 1),
			BPoint(pad + barsWidth + 1, pad - 1));
		SetPenSize(1);
	}

	BString title;
	BString detail;
	_StatusStrings(title, detail);

	font_height plainHeight;
	be_plain_font->GetHeight(&plainHeight);

	float textX = pad + barsWidth + 18;
	SetFont(be_bold_font);
	SetHighColor(0, 0, 0);
	float baseline1 = pad + plainHeight.ascent;
	DrawString(title.String(), BPoint(textX, baseline1));

	SetFont(be_plain_font);
	SetHighColor(110, 110, 110);
	float baseline2 = baseline1 + plainHeight.descent + 4
		+ plainHeight.ascent;
	DrawString(detail.String(), BPoint(textX, baseline2));
	SetHighColor(0, 0, 0);
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
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(B_TRANSLATE("Quit"),
		new BMessage(kMsgQuit)));

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
	*width = 16;
	*height = 16;
	return B_OK;
}
