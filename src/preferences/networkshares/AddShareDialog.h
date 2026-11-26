/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _ADD_SHARE_DIALOG_H
#define _ADD_SHARE_DIALOG_H


#include <Window.h>

#include "ShareCore.h"


class BButton;
class BMenu;
class BMenuField;
class BPopUpMenu;
class BTextControl;


class AddShareDialog : public BWindow {
public:
					AddShareDialog(BWindow* parent,
						const VosNetworkShareSpec* fill = NULL);

	virtual	void	MessageReceived(BMessage* message);
	virtual	void	Show();
	virtual	bool	QuitRequested();

			const VosNetworkShareSpec& Spec() const { return fSpec; }

private:
			void	_FillFromSpec(const VosNetworkShareSpec& spec);
			bool	_BuildSpecFromFields(VosNetworkShareSpec* spec,
						BString* error);
			void	_AppendLANEntry(BMenu* menu, const char* label,
						const char* server, const char* exportName);

	BWindow*		fParent;
	VosNetworkShareSpec	fSpec;
	BPopUpMenu*		fProtocolMenu;
	BMenuField*		fProtocolField;
	BPopUpMenu*		fLANMenu;
	BMenuField*		fLANField;
	BTextControl*	fServerField;
	BTextControl*	fExportField;
	BTextControl*	fMountField;
	BPopUpMenu*		fModeMenu;
	BMenuField*		fModeField;
	BTextControl*	fOptionsField;
	BTextControl*	fUserField;
	BTextControl*	fPasswordField;
	BTextControl*	fDomainField;
	BButton*		fOKButton;
	BButton*		fCancelButton;
	bool			fAccepting;
};


#endif	// _ADD_SHARE_DIALOG_H
