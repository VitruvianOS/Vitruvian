/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _ADD_RULE_WINDOW_H
#define _ADD_RULE_WINDOW_H


#include <Messenger.h>
#include <String.h>
#include <Window.h>


class BButton;
class BMenuField;
class BPopUpMenu;
class BTextControl;


// Sends kMsgRulesChosen to the target with one "rule" message per
// allowed port (see FirewallPutRule).
class AddRuleWindow : public BWindow {
public:
								AddRuleWindow(BWindow* parent,
									const BMessenger& target);

	virtual	void				MessageReceived(BMessage* message);

private:
			void				_UpdateCustom();
			void				_Accept();

			BMessenger			fTarget;
			BPopUpMenu*			fServiceMenu;
			BPopUpMenu*			fProtocolMenu;
			BMenuField*			fProtocolField;
			BTextControl*		fPortControl;
			BButton*			fAddButton;
};


#endif // _ADD_RULE_WINDOW_H
