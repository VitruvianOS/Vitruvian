/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _PASSWORDS_VIEW_H
#define _PASSWORDS_VIEW_H


#include <CardLayout.h>
#include <GroupView.h>
#include <KeyStore.h>
#include <List.h>
#include <MessageRunner.h>
#include <ObjectList.h>
#include <String.h>
#include <View.h>

#include <ColumnListView.h>


class BCardLayout;
class BMenuBar;
class BMenuItem;
class BStringView;
class BTextControl;
class EntryItem;
class OpenEntry;


class PasswordsView : public BGroupView {
public:
								PasswordsView();
								~PasswordsView();

	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_LoadKeyrings();
			void				_LoadEntries(const char* keyring);
			void				_OpenEntry(int32 index);
			void				_UpdateMenus();
			void				_DeleteEntry();
			void				_Error(const char* message);
			void				_CopyToClipboard(const BString& text,
									int32 clearAfterSeconds);
			void				_ClearClipboard();
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
			BObjectList<OpenEntry>	fOpenEntries;

			BColumnListView*	fGroupList;
			BColumnListView*	fEntryList;
			BCardLayout*		fEntryCards;
			BTextControl*		fSearchControl;
			BMenuItem*			fNewItem;
			BMenuItem*			fOpenItem;
			BMenuItem*			fCopyUserItem;
			BMenuItem*			fCopyPasswordItem;
			BMenuItem*			fDeleteItem;
			BMessageRunner*		fClipboardRunner;
			int32				fClipboardMessageWhat;
};


#endif	/* _PASSWORDS_VIEW_H */
