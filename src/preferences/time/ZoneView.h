/*
 * Copyright 2004-2012, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Mike Berg <mike@berg-net.us>
 *		Julun <host.haiku@gmx.de>
 *		Hamish Morrison <hamish@lavabit.com>
 */
#ifndef ZONE_VIEW_H
#define ZONE_VIEW_H


#include <LayoutBuilder.h>
#include <TimeFormat.h>
#include <TimeZone.h>


class BButton;
class BMessage;
class BOutlineListView;
class BRadioButton;
class BTimeZone;
class TimeZoneListItem;
class TimeZoneListView;
class TTZDisplay;


class TimeZoneView : public BGroupView {
public:
								TimeZoneView(const char* name);
	virtual						~TimeZoneView();

	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);
			bool				CheckCanRevert();

protected:
	virtual void				DoLayout();

private:
			void				_UpdateDateTime(BMessage* message);

			void				_StartLoadSystemZone();
			void				_StartLoadLocalRTC();
			void				_StartSetLocalRTC();
			void				_ApplySystemZone(const char* systemZoneId);
			void				_SetSystemTimeZone();

			void				_UpdatePreview();
			void				_UpdateCurrent();
			BString				_FormatTime(const BTimeZone& timeZone);

			void				_ShowOrHidePreview();

			void				_InitView();
			void				_BuildZoneMenu(const char* systemZoneId);

			void				_Revert();

			TimeZoneListView*	fZoneList;
			BButton*			fSetZone;
			TTZDisplay*			fCurrent;
			TTZDisplay*			fPreview;
			BRadioButton*		fLocalTime;
			BRadioButton*		fGmtTime;

			int32				fLastUpdateMinute;
			bool				fUseGmtTime;
			bool				fOldUseGmtTime;

			TimeZoneListItem*	fCurrentZoneItem;
			TimeZoneListItem*	fOldZoneItem;
			TimeZoneListItem*	fPendingZoneItem;
			bool				fInitialized;
			bool				fLocalRTCPending;

			BTimeFormat			fTimeFormat;
};


#endif // ZONE_VIEW_H
