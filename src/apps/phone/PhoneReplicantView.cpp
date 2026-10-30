/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "PhoneReplicantView.h"

#include <Application.h>
#include <Catalog.h>
#include <Dragger.h>
#include <Font.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <Roster.h>

#include <string.h>

#include <telephony/Modem.h>
#include <telephony/TelephonyRoster.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PhoneReplicant"


// Sized for the Deskbar tray: fMaxReplicantHeight is one mini icon, and
// a strip wider than the tray row would be clipped on a vertical bar.
static const float kReplicantWidth = 100.0f;
static const float kReplicantHeight = 16.0f;


const char* PhoneReplicantView::kAppSignature
	= "application/x-vnd.vos-Phone";


PhoneReplicantView::PhoneReplicantView(BRect frame, int32 resizingMode,
	bool inDeskbar)
	:
	BView(frame, "Phone", resizingMode, B_WILL_DRAW | B_FRAME_EVENTS),
	fRoster(NULL),
	fInDeskbar(inDeskbar)
{
	_Init(inDeskbar);
}


PhoneReplicantView::PhoneReplicantView(BMessage* archive)
	:
	BView(archive),
	fRoster(NULL),
	fInDeskbar(false)
{
	// Through instantiate_deskbar_item(), so tray mode has to be
	// recovered from who is hosting us.
	app_info info;
	if (be_app->GetAppInfo(&info) == B_OK
			&& !strcasecmp(info.signature, "application/x-vnd.Be-TSKB"))
		fInDeskbar = true;

	_Init(fInDeskbar);
}


PhoneReplicantView::~PhoneReplicantView()
{
}


void
PhoneReplicantView::_Init(bool inDeskbar)
{
	fRoster = BTelephonyRoster::Default();

	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	SetLowColor(ViewColor());

	// A replicant restored from an archive brings its own dragger back
	// with it; only build one here when there was none.
	if (!inDeskbar && FindView("_dragger_") == NULL) {
		BRect frame(Bounds());
		frame.top = frame.bottom - 7;
		frame.left = frame.right - 7;
		BDragger* dragger = new BDragger(frame, this,
			B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM);
		AddChild(dragger);
	}
}


BArchivable*
PhoneReplicantView::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "PhoneReplicantView"))
		return NULL;

	return new PhoneReplicantView(archive);
}


status_t
PhoneReplicantView::Archive(BMessage* archive, bool deep) const
{
	status_t status = BView::Archive(archive, deep);
	if (status == B_OK)
		status = archive->AddString("add_on", kAppSignature);
	if (status == B_OK)
		status = archive->AddString("class", "PhoneReplicantView");

	return status;
}


void
PhoneReplicantView::AttachedToWindow()
{
	BView::AttachedToWindow();

	BMessenger self(this);
	fRoster->StartWatching(self, kTelephonyWatchAll);
	_SyncFromRoster();
}


void
PhoneReplicantView::DetachedFromWindow()
{
	BMessenger self(this);
	fRoster->StopWatching(self);

	BView::DetachedFromWindow();
}


void
PhoneReplicantView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kTelephonyMsgCallAdded:
		case kTelephonyMsgCallStateChanged:
		case kTelephonyMsgCallRemoved:
			_SyncFromRoster();
			Invalidate();
			break;
		default:
			BView::MessageReceived(message);
			break;
	}
}


void
PhoneReplicantView::_SyncFromRoster()
{
	fStatus = "";

	BModem* modem = fRoster->ModemAt(0);
	if (modem == NULL)
		return;

	uint32 state = modem->CallState();
	if (state == B_TELEPHONY_CALL_UNKNOWN
			|| state == B_TELEPHONY_CALL_TERMINATED)
		return;

	fStatus = modem->CallStateName();
	const char* number = modem->CallNumber();
	if (number != NULL && number[0] != '\0')
		fStatus << " " << number;
}


void
PhoneReplicantView::Draw(BRect updateRect)
{
	(void)updateRect;

	FillRect(Bounds());

	// Receiver glyph: both ear pieces and the bar between them.
	SetHighColor(70, 70, 70);
	SetPenSize(2);
	StrokeLine(BPoint(5, 11), BPoint(11, 5));
	FillEllipse(BPoint(5, 11), 2, 2);
	FillEllipse(BPoint(11, 5), 2, 2);

	BFont font(be_plain_font);
	font.SetSize(10);
	SetFont(&font);
	SetHighColor(0, 0, 0);

	BString label(B_TRANSLATE("Phone"));
	if (!fStatus.IsEmpty())
		label << ": " << fStatus;

	DrawString(label.String(), BPoint(17, Bounds().Height() - 4));

	SetPenSize(1);
	SetHighColor(130, 130, 130);
	StrokeRect(Bounds());
}


void
PhoneReplicantView::GetPreferredSize(float* _width, float* _height)
{
	*_width = kReplicantWidth;
	*_height = kReplicantHeight;
}


void
PhoneReplicantView::MouseDown(BPoint where)
{
	(void)where;
	_OpenDialer();
}


void
PhoneReplicantView::_OpenDialer()
{
	// Never touch be_app: a replicant runs inside the host's team.
	be_roster->Launch(kAppSignature);
}


// #pragma mark - Replicant entry points


extern "C" _EXPORT BView*
instantiate_deskbar_item(float maxWidth, float /*maxHeight*/)
{
	float width = maxWidth > 0 && maxWidth < kReplicantWidth
		? maxWidth : kReplicantWidth;
	return new PhoneReplicantView(BRect(0, 0, width - 1,
		kReplicantHeight - 1), B_FOLLOW_LEFT | B_FOLLOW_TOP, true);
}


extern "C" _EXPORT status_t
instantiate_deskbar_item_for_width(float* width, float* height)
{
	*width = kReplicantWidth;
	*height = kReplicantHeight;
	return B_OK;
}
