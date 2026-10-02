/*
 * Copyright 2004-2007, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Mike Berg <mike@berg-net.us>
 *		Julun <host.haiku@gmx.de>
 */


#include "BaseView.h"

#include <Catalog.h>
#include <DateTime.h>
#include <Messenger.h>
#include <OS.h>

#include "TimeMessages.h"
#include "TimeWindow.h"
#include "TimedatedAsync.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Time"


using BPrivate::BDateTime;
using BPrivate::B_LOCAL_TIME;
using BPrivate::kTimedatedOpSetTime;
using BPrivate::TimedatedAsyncRun;


TTimeBaseView::TTimeBaseView(const char* name)
	:
	BGroupView(name, B_VERTICAL, 0),
	fMessage(H_TIME_UPDATE)
{
	SetFlags(Flags() | B_PULSE_NEEDED);
}


TTimeBaseView::~TTimeBaseView()
{
}


void
TTimeBaseView::Pulse()
{
	if (IsWatched())
		_SendNotices();
}


void
TTimeBaseView::AttachedToWindow()
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLowUIColor(ViewUIColor());
}


void
TTimeBaseView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kTimedatedResult:
		{
			int32 op;
			if (message->FindInt32("op", &op) != B_OK
					|| op != kTimedatedOpSetTime)
				break;

			status_t status;
			message->FindInt32("status", &status);
			if (status != B_OK) {
				const char* error = NULL;
				message->FindString("error", &error);
				ShowTimeError(B_TRANSLATE("Could not set the date and "
					"time."), status, error);
			}
			break;
		}

		default:
			BGroupView::MessageReceived(message);
			break;
	}
}


void
TTimeBaseView::ChangeTime(BMessage* message)
{
	bool isTime;
	if (message->FindBool("time", &isTime) != B_OK)
		return;

	BDateTime dateTime = BDateTime::CurrentDateTime(B_LOCAL_TIME);

	if (isTime) {
		BTime time = dateTime.Time();
		int32 hour;
		if (message->FindInt32("hour", &hour) != B_OK)
			hour  = time.Hour();

		int32 minute;
		if (message->FindInt32("minute", &minute) != B_OK)
			minute = time.Minute();

		int32 second;
		if (message->FindInt32("second", &second) != B_OK)
			second = time.Second();

		time.SetTime(hour, minute, second);
		dateTime.SetTime(time);
	} else {
		BDate date = dateTime.Date();
		int32 day;
		if (message->FindInt32("day", &day) != B_OK)
			day = date.Day();

		int32 year;
		if (message->FindInt32("year", &year) != B_OK)
			year = date.Year();

		int32 month;
		if (message->FindInt32("month", &month) != B_OK)
			month = date.Month();

		date.SetDate(year, month, day);
		dateTime.SetDate(date);
	}

	// timedated owns the system clock; the worker keeps polkit off the
	// window thread.
	BMessage args;
	args.AddInt64("usec", (int64)dateTime.Time_t() * 1000000);
	args.AddBool("relative", false);
	TimedatedAsyncRun(kTimedatedOpSetTime, args, BMessenger(this));
}


void
TTimeBaseView::_SendNotices()
{
	fMessage.MakeEmpty();

	BDate date = BDate::CurrentDate(B_LOCAL_TIME);
	fMessage.AddInt32("day", date.Day());
	fMessage.AddInt32("year", date.Year());
	fMessage.AddInt32("month", date.Month());

	BTime time = BTime::CurrentTime(B_LOCAL_TIME);
	fMessage.AddInt32("hour", time.Hour());
	fMessage.AddInt32("minute", time.Minute());
	fMessage.AddInt32("second", time.Second());

	SendNotices(H_TM_CHANGED, &fMessage);
}
