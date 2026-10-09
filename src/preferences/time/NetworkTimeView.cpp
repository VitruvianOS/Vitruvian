/*
 * Copyright 2011-2014 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *		Hamish Morrison, hamish@lavabit.com
 *		John Scipione, jscipione@gmail.com
 */


#include "NetworkTimeView.h"

#include <Catalog.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <Messenger.h>
#include <Message.h>
#include <StringView.h>
#include <Window.h>

#include "TimeMessages.h"
#include "TimeWindow.h"
#include "TimedatedAsync.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Time"


using BPrivate::kTimedatedOpGetNTP;
using BPrivate::kTimedatedOpGetNTPSynced;
using BPrivate::kTimedatedOpSetNTP;
using BPrivate::TimedatedAsyncRun;


NetworkTimeView::NetworkTimeView(const char* name)
	:
	BGroupView(name, B_VERTICAL, B_USE_DEFAULT_SPACING),
	fNTPCheckBox(NULL),
	fStatusView(NULL),
	fNTPEnabled(false),
	fNTPSynced(false),
	fNTPOpPending(false),
	fNTPAvailable(true)
{
	_InitView();
}


NetworkTimeView::~NetworkTimeView()
{
}


void
NetworkTimeView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgToggleNTP:
			_ApplyNTP(fNTPCheckBox->Value() == B_CONTROL_ON);
			break;

		case kMsgNTPStateChanged:
		{
			// notification from the window; do not re-apply
			bool enable;
			if (message->FindBool("ntp", &enable) == B_OK) {
				fNTPEnabled = enable;
				fNTPCheckBox->SetValue(enable ? B_CONTROL_ON
					: B_CONTROL_OFF);
				BMessage args;
				TimedatedAsyncRun(kTimedatedOpGetNTPSynced, args,
					BMessenger(this));
				_UpdateStatus();
			}
			break;
		}

		case kMsgRevert:
			_ApplyNTP(fNTPEnabled);
			break;

		case kTimedatedResult:
		{
			int32 op;
			if (message->FindInt32("op", &op) != B_OK)
				break;

			status_t status;
			message->FindInt32("status", &status);
			const char* error = NULL;
			message->FindString("error", &error);

			switch (op) {
				case kTimedatedOpGetNTP:
				{
					bool enabled = false;
					message->FindBool("ntp", &enabled);
					if (status == B_OK) {
						bool changed = fNTPEnabled != enabled;
						fNTPEnabled = enabled;
						fNTPAvailable = true;
						fNTPCheckBox->SetValue(enabled ? B_CONTROL_ON
							: B_CONTROL_OFF);
						if (changed)
							_NotifyNTPChanged();
					} else {
						_SetNTPUnavailable();
					}
					_UpdateStatus();
					break;
				}

				case kTimedatedOpGetNTPSynced:
				{
					if (status == B_OK)
						message->FindBool("synced", &fNTPSynced);
					_UpdateStatus();
					break;
				}

				case kTimedatedOpSetNTP:
				{
					fNTPOpPending = false;
					fNTPCheckBox->SetEnabled(true);

					bool enable = false;
					message->FindBool("enable", &enable);
					if (status != B_OK) {
						if (status == B_NOT_SUPPORTED) {
							// GetNTP would only report "off", not missing.
							_SetNTPUnavailable();
							ShowTimeError(B_TRANSLATE("Network time is not "
								"available on this system. No NTP service is "
								"installed; set the date and time manually "
								"on the Date and time tab."), status, NULL);
							break;
						}
						ShowTimeError(
							B_TRANSLATE("Could not change network time."),
							status, error);
						// leave the checkbox at the system state
						BMessage args;
						TimedatedAsyncRun(kTimedatedOpGetNTP, args,
							BMessenger(this));
						TimedatedAsyncRun(kTimedatedOpGetNTPSynced, args,
							BMessenger(this));
						break;
					}

					fNTPAvailable = true;
					fNTPEnabled = enable;
					fNTPSynced = false;
					BMessage args;
					TimedatedAsyncRun(kTimedatedOpGetNTPSynced, args,
						BMessenger(this));
					_UpdateStatus();
					_NotifyNTPChanged();
					break;
				}

				default:
					break;
			}
			break;
		}

		default:
			BGroupView::MessageReceived(message);
			break;
	}
}


void
NetworkTimeView::AttachedToWindow()
{
	fNTPCheckBox->SetTarget(this);
	_StartLoadState();
}


bool
NetworkTimeView::CheckCanRevert()
{
	return (fNTPCheckBox->Value() == B_CONTROL_ON) != fNTPEnabled;
}


void
NetworkTimeView::_InitView()
{
	fNTPCheckBox = new BCheckBox("ntp",
		B_TRANSLATE("Set time and date automatically (network time)"),
		new BMessage(kMsgToggleNTP));

	fStatusView = new BStringView("ntpStatus", "");

	BLayoutBuilder::Group<>(this)
		.Add(fNTPCheckBox)
		.Add(fStatusView)
		.AddGlue()
		.SetInsets(B_USE_WINDOW_SPACING, B_USE_WINDOW_SPACING,
			B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING);
}


void
NetworkTimeView::_StartLoadState()
{
	BMessage args;
	TimedatedAsyncRun(kTimedatedOpGetNTP, args, BMessenger(this));
	TimedatedAsyncRun(kTimedatedOpGetNTPSynced, args, BMessenger(this));
}


void
NetworkTimeView::_UpdateStatus()
{
	BString status;
	if (!fNTPAvailable) {
		status = B_TRANSLATE("Network time is not available on this "
			"system. Set the date and time manually on the Date and time "
			"tab.");
	} else if (fNTPCheckBox->Value() == B_CONTROL_ON) {
		status = B_TRANSLATE("Network time is on. The clock is kept by "
			"systemd-timesyncd; manual date and time controls are disabled.");
		if (fNTPSynced)
			status << "\n" << B_TRANSLATE("The clock is synchronized.");
		else
			status << "\n" << B_TRANSLATE("The clock is not synchronized yet.");
	} else {
		status = B_TRANSLATE("Network time is off. Set the date and time "
			"manually on the Date and time tab.");
	}
	fStatusView->SetText(status.String());
}


void
NetworkTimeView::_SetNTPUnavailable()
{
	fNTPAvailable = false;
	fNTPEnabled = false;
	fNTPSynced = false;
	fNTPCheckBox->SetValue(B_CONTROL_OFF);
	fNTPCheckBox->SetEnabled(false);
}


void
NetworkTimeView::_NotifyNTPChanged()
{
	BMessage message(kMsgNTPStateChanged);
	message.AddBool("ntp", fNTPEnabled);
	Window()->PostMessage(&message);
}


void
NetworkTimeView::_ApplyNTP(bool enable)
{
	if (!fNTPAvailable || fNTPOpPending)
		return;

	fNTPOpPending = true;
	fNTPCheckBox->SetEnabled(false);

	BMessage args;
	args.AddBool("enable", enable);
	TimedatedAsyncRun(kTimedatedOpSetNTP, args, BMessenger(this));
}
