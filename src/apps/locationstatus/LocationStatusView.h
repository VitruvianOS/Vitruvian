/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef LOCATION_STATUS_VIEW_H
#define LOCATION_STATUS_VIEW_H


#include <Messenger.h>
#include <View.h>
#include <Message.h>
#include <String.h>


class BBitmap;
class BMenu;
class BMessageRunner;
class LocationProvider;
class LocationSettings;


enum {
	kMsgStatusChanged = 'LcUc',
	kMsgOpenLocationPreferences = 'LcOp',
	kMsgRemoveFromDeskbar = 'LcRm',
	kMsgReloadSettings = 'LcRl',
};


enum {
	kLocationIndicatorOffline = 0,
	kLocationIndicatorNoFix,
	kLocationIndicator2DFix,
	kLocationIndicator3DFix,
};


class LocationStatusView : public BView {
public:
						LocationStatusView(BRect frame, int32 resizingMode,
							bool inDeskbar = false);
						LocationStatusView(BMessage* archive);
	virtual				~LocationStatusView();

	static	LocationStatusView* Instantiate(BMessage* archive);
	virtual	status_t	Archive(BMessage* archive, bool deep = true) const;

	virtual	void		AttachedToWindow();
	virtual	void		DetachedFromWindow();

	virtual	void		MessageReceived(BMessage* message);
	virtual	void		Draw(BRect updateRect);
	virtual	void		MouseDown(BPoint where);

private:
	void				_ApplyStatus(BMessage* message);
	void				_ShowMenu(BPoint where);
	void				_OpenLocationPreferences();
	void				_RemoveFromDeskbar();
	void				_Quit();
	void				_ReloadSettings();
	static	int32		_RemoveFromDeskbarThread(void* data);

	bool				fInDeskbar;
	int32				fIndicator;
	LocationProvider*	fProvider;
	LocationSettings*	fSettings;
	BMessageRunner*		fSettingsRunner;
	bool				fWantsEnabled;
	BString				fSourceHost;
	uint16				fSourcePort;
};


#endif	// LOCATION_STATUS_VIEW_H
