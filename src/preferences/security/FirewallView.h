/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _FIREWALL_VIEW_H
#define _FIREWALL_VIEW_H


#include "FirewallModel.h"

#include <View.h>


class BButton;
class BCheckBox;
class BGroupView;
class BListView;
class BMenuField;
class BPopUpMenu;
class BStringView;


class FirewallView : public BView {
public:
								FirewallView();
	virtual						~FirewallView();

	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_LoadSettings();
			void				_SaveSettings();
			void				_Populate(const FirewallState& state);
			void				_Submit(int32 op, const FirewallState& state);
			void				_RefreshRuleList();
			void				_UpdateControls();
			void				_UpdateHeader();
			void				_ApplyResult(BMessage* message);
			void				_ToggleDeskbar(bool show);
			void				_ToggleAdvanced();
			void				_ReadStamp();
			void				_AddRules(BMessage* message);

			BStringView*		fStateLabel;
			BStringView*		fHintLabel;
			BCheckBox*			fEnabledCheck;
			BMenuField*			fProfileField;
			BPopUpMenu*			fProfileMenu;
			BListView*			fRuleList;
			BButton*			fAddButton;
			BButton*			fRemoveButton;
			BCheckBox*			fDeskbarCheck;
			BButton*			fAdvancedButton;
			BGroupView*			fAdvancedGroup;
			BMenuField*			fOutPolicyField;
			BPopUpMenu*			fOutPolicyMenu;
			BCheckBox*			fLoggingCheck;
			BButton*			fRestoreButton;
			BStringView*		fBackendLabel;
			BStringView*		fStatusLabel;

			FirewallState		fState;
			BString				fBackend;
			bool				fPopulating;
			bool				fBusy;
			bool				fHasBackend;
			bool				fSeeded;
			bool				fFetchPending;
			bool				fKeepStatus;
};


#endif // _FIREWALL_VIEW_H
