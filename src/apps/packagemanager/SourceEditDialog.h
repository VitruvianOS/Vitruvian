/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef SOURCE_EDIT_DIALOG_H
#define SOURCE_EDIT_DIALOG_H

#include <Messenger.h>
#include <String.h>
#include <Window.h>


class BMenu;
class BTextControl;


// Modal add/edit form for one user source. Validates in the UI layer,
// then posts kMsgSourceAdd (slug, types, uris, suites, components,
// signed_by) to its target; the helper revalidates everything as root.
class SourceEditDialog : public BWindow {
	typedef BWindow Inherited;
public:
								SourceEditDialog(BWindow* parent,
									const BMessenger& target, bool edit,
									const BMessage* entry = NULL);
	virtual						~SourceEditDialog();

	virtual	void				MessageReceived(BMessage* message);

private:
			void				_BuildLayout(const BMessage* entry);
			void				_MarkTypes(const char* types);
			void				_MarkSignedBy(const char* path);
			bool				_Validate(BString* error);

			BMessenger			fTarget;
			bool				fEdit;
			BTextControl*		fSlugControl;
			BTextControl*		fUrisControl;
			BTextControl*		fSuitesControl;
			BTextControl*		fComponentsControl;
			BMenu*				fTypesMenu;
			BMenu*				fSignedByMenu;
};


#endif // SOURCE_EDIT_DIALOG_H
