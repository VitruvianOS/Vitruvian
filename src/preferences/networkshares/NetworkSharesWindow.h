/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _NETWORK_SHARES_WINDOW_H
#define _NETWORK_SHARES_WINDOW_H


#include <Window.h>

#include "ShareCore.h"
#include "ShareListItem.h"
#include "ShareSettings.h"


class BButton;
class BListView;
class BScrollView;
class BStringView;


enum {
	kMsgSharesChanged		= 'NSCh',
	kMsgAdd				= 'NSAd',
	kMsgEdit				= 'NSEt',
	kMsgRemove			= 'NSRm',
	kMsgMount			= 'NSMt',
	kMsgUnmount			= 'NSUm',
	kMsgRefresh			= 'NSRf',
	kMsgSelectionChanged	= 'NSSc',
	kMsgHelperDone		= 'NSHp',
	kMsgOpenAdd			= 'NSOA',
	kMsgAddDialogDone		= 'NSAD',
};


class NetworkSharesWindow : public BWindow {
public:
					NetworkSharesWindow();

	virtual	void	MessageReceived(BMessage* message);
	virtual	bool	QuitRequested();

			void	LoadShares();
			void	UpdateButtons();
			void	OpenAddDialog(const VosNetworkShareSpec* fill = NULL);

private:
			void	_RefreshList();
			bool	_GetSelected(ShareListEntry* entry);
			void	_RunHelper(const char* command, const ShareListEntry& entry,
						const BString& extraArgs = BString());
			void	_ShowError(const BString& message);

	BListView*		fListView;
	BScrollView*	fScrollView;
	BButton*		fAddButton;
	BButton*		fEditButton;
	BButton*		fRemoveButton;
	BButton*		fMountButton;
	BButton*		fUnmountButton;
	BButton*		fRefreshButton;
	BStringView*	fStatusView;
	ShareSettings	fSettings;
	bool			fHelperBusy;
};


#endif	// _NETWORK_SHARES_WINDOW_H
