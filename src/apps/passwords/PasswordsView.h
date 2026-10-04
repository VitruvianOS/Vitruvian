/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _PASSWORDS_VIEW_H
#define _PASSWORDS_VIEW_H


#include <GroupView.h>
#include <KeyStore.h>
#include <List.h>
#include <MessageRunner.h>
#include <ObjectList.h>
#include <String.h>
#include <View.h>

#include <ColumnListView.h>


class BButton;
class BStringView;
class BTextControl;
class BTextView;
class EntryItem;


class PasswordsView : public BGroupView {
public:
								PasswordsView();
								~PasswordsView();

	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_LoadKeyrings();
			void				_LoadEntries(const char* keyring);
			void				_ShowEntry(int32 index);
			void				_SaveCurrentEntry();
			void				_CopyToClipboard(const BString& text,
									int32 clearAfterSeconds);
			void				_ClearClipboard();
			void				_UpdateTotp();
			void				_GeneratePassword();
			void				_ExportDatabase();
			void				_ImportDatabase();
			void				_ClearRows(BList& rows,
									BColumnListView* list);

			BKeyStore			fKeyStore;
			BObjectList<BString>	fKeyrings;
			BObjectList<EntryItem>	fEntries;
			BList				fGroupRows;
			BList				fEntryRows;
			BString				fCurrentKeyring;
			bool				fHaveCurrent;

			BColumnListView*	fGroupList;
			BColumnListView*	fEntryList;
			BTextControl*		fSearchControl;
			BTextControl*		fTitleControl;
			BTextControl*		fUserControl;
			BTextControl*		fPasswordControl;
			BTextControl*		fUrlControl;
			BTextView*			fNotesControl;
			BTextControl*		fTotpControl;
			BStringView*		fTotpCodeView;
			BStringView*		fStatusLabel;
			BMessageRunner*		fTotpRunner;
			BMessageRunner*		fClipboardRunner;
			int32				fClipboardMessageWhat;
};


#endif	/* _PASSWORDS_VIEW_H */
