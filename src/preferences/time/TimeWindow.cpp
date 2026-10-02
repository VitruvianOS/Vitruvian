/*
 * Copyright 2004-2011, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Andrew McCall <mccall@@digitalparadise.co.uk>
 *		Julun <host.haiku@gmx.de>
 *		Hamish Morrison <hamish@lavabit.com>
 */

#include "TimeWindow.h"

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <LocaleRoster.h>
#include <Messenger.h>
#include <Message.h>
#include <Screen.h>
#include <SeparatorView.h>
#include <String.h>
#include <TabView.h>

#include "BaseView.h"
#include "ClockView.h"
#include "DateTimeView.h"
#include "NetworkTimeView.h"
#include "TimeMessages.h"
#include "TimeSettings.h"
#include "TimedatedAsync.h"
#include "ZoneView.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Time"


using BPrivate::kTimedatedOpGetNTP;
using BPrivate::TimedatedAsyncRun;


void
ShowTimeError(const char* what, status_t status, const char* dbError)
{
	BString message;
	if (status == B_PERMISSION_DENIED || status == B_NOT_ALLOWED)
		message = B_TRANSLATE("Authentication was required and was not "
			"granted.");
	else
		message = B_TRANSLATE("Could not change system time settings.");
	message << "\n\n" << what;
	if (dbError != NULL && *dbError != '\0')
		message << "\n\n" << dbError;

	BAlert* alert = new BAlert(B_TRANSLATE("Time"), message.String(),
		B_TRANSLATE("OK"));
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go();
}


TTimeWindow::TTimeWindow()
	:
	BWindow(BRect(0, 0, 0, 0), B_TRANSLATE_SYSTEM_NAME("Time"), B_TITLED_WINDOW,
		B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS)
{
	_InitWindow();
	_AlignWindow();

	AddShortcut('A', B_COMMAND_KEY, new BMessage(B_ABOUT_REQUESTED));
}


TTimeWindow::~TTimeWindow()
{
}


bool
TTimeWindow::QuitRequested()
{
	TimeSettings().SetLeftTop(Frame().LeftTop());

	fBaseView->StopWatchingAll(fTimeZoneView);
	fBaseView->StopWatchingAll(fDateTimeView);

	be_app->PostMessage(B_QUIT_REQUESTED);

	return BWindow::QuitRequested();
}


void
TTimeWindow::MessageReceived(BMessage* message)
{
	switch(message->what) {
		case H_USER_CHANGE:
			fBaseView->ChangeTime(message);
			// To make sure no old time message is in the queue
			_SendTimeChangeFinished();
			_SetRevertStatus();
			break;

		case B_ABOUT_REQUESTED:
			be_app->PostMessage(B_ABOUT_REQUESTED);
			break;

		case B_LOCALE_CHANGED:
		{
			BLocaleRoster::Default()->Refresh();
			fDateTimeView->MessageReceived(message);
			break;
		}

		case kMsgRevert:
			fDateTimeView->MessageReceived(message);
			fTimeZoneView->MessageReceived(message);
			fNetworkTimeView->MessageReceived(message);
			fClockView->MessageReceived(message);
			fRevertButton->SetEnabled(false);
			break;

		case kRTCUpdate:
			fDateTimeView->MessageReceived(message);
			fTimeZoneView->MessageReceived(message);
			_SetRevertStatus();
			break;

		case kMsgChange:
			_SetRevertStatus();
			break;

		case kMsgNTPStateChanged:
		{
			// NetworkTimeView posts this; NTP on disables manual date and time.
			bool ntpOn = false;
			message->FindBool("ntp", &ntpOn);
			fDateTimeView->SetManualTimeAllowed(!ntpOn);
			fNetworkTimeView->MessageReceived(message);
			_SetRevertStatus();
			break;
		}

		case kTimedatedResult:
		{
			int32 op;
			if (message->FindInt32("op", &op) != B_OK
					|| op != kTimedatedOpGetNTP)
				break;

			status_t status;
			if (message->FindInt32("status", &status) != B_OK
					|| status != B_OK)
				break;

			bool ntpOn = false;
			if (message->FindBool("ntp", &ntpOn) == B_OK)
				fDateTimeView->SetManualTimeAllowed(!ntpOn);
			break;
		}

		case kSelectClockTab:
			// focus the clock tab (last one)
			fTabView->Select(fTabView->CountTabs() - 1);
			break;

		case kShowHideTime:
			fClockView->MessageReceived(message);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
TTimeWindow::_InitWindow()
{
	SetPulseRate(500000);

	fDateTimeView = new DateTimeView(B_TRANSLATE("Date and time"));
	fTimeZoneView = new TimeZoneView(B_TRANSLATE("Time zone"));
	fNetworkTimeView = new NetworkTimeView(B_TRANSLATE("Network time"));
	fClockView = new ClockView(B_TRANSLATE("Clock"));

	fBaseView = new TTimeBaseView("baseView");
	fBaseView->StartWatchingAll(fDateTimeView);
	fBaseView->StartWatchingAll(fTimeZoneView);

	fTabView = new BTabView("tabView", B_WIDTH_FROM_WIDEST);
	fTabView->AddTab(fDateTimeView);
	fTabView->AddTab(fTimeZoneView);
	fTabView->AddTab(fNetworkTimeView);
	fTabView->AddTab(fClockView);
	fTabView->SetBorder(B_NO_BORDER);

	fBaseView->AddChild(fTabView);

	fRevertButton = new BButton("revert", B_TRANSLATE("Revert"),
		new BMessage(kMsgRevert));
	fRevertButton->SetEnabled(false);
	fRevertButton->SetTarget(this);
	fRevertButton->SetExplicitAlignment(
		BAlignment(B_ALIGN_LEFT, B_ALIGN_MIDDLE));

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(0, B_USE_DEFAULT_SPACING, 0, 0)
		.Add(fBaseView)
		.Add(new BSeparatorView(B_HORIZONTAL))
		.AddGroup(B_HORIZONTAL)
			.Add(fRevertButton)
			.SetInsets(B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING,
				B_USE_DEFAULT_SPACING, B_USE_WINDOW_SPACING);

	_SyncNTPState();
}


void
TTimeWindow::_AlignWindow()
{
	MoveTo(TimeSettings().LeftTop());
	MoveOnScreen();
}


void
TTimeWindow::_SendTimeChangeFinished()
{
	BMessenger messenger(fDateTimeView);
	BMessage msg(kChangeTimeFinished);
	messenger.SendMessage(&msg);
}


void
TTimeWindow::_SetRevertStatus()
{
	fRevertButton->SetEnabled(fDateTimeView->CheckCanRevert()
		|| fTimeZoneView->CheckCanRevert()
		|| fNetworkTimeView->CheckCanRevert()
		|| fClockView->CheckCanRevert());
}


void
TTimeWindow::_SyncNTPState()
{
	// Polkit can block a timedated Get; keep it off the window thread.
	BMessage args;
	TimedatedAsyncRun(kTimedatedOpGetNTP, args, BMessenger(this));
}
