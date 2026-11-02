/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _ENTRY_WINDOW_H
#define _ENTRY_WINDOW_H


#include <KeyStore.h>
#include <Messenger.h>
#include <String.h>
#include <Window.h>


class BCheckBox;
class BMessageRunner;
class BStringView;
class BTextControl;
class BTextView;


static const uint32 kMsgEntrySaved		= 'EnSv';
static const uint32 kMsgEntryClosed		= 'EnCl';
static const uint32 kMsgEntryCopy		= 'EnCp';
static const uint32 kMsgEntryActivate	= 'EnAc';

static const char* const kUrlSuffix = "/url";
static const char* const kNotesSuffix = "/notes";


class EntryWindow : public BWindow {
public:
								EntryWindow(const BMessenger& target,
									const char* keyring, const char* title,
									const char* username,
									const char* password, const char* totp,
									const char* url, const char* notes);
								~EntryWindow();

	virtual	bool				QuitRequested();
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_Save();
			void				_UpdateTotp();
			void				_GeneratePassword();
			void				_StoreExtra(const BString& title,
									const char* suffix, const BString& username,
									const BString& value);
			void				_RemoveEntry(const BString& title,
									const BString& username);

			BMessenger			fTarget;
			BKeyStore			fKeyStore;
			BString				fKeyring;
			BString				fOrigTitle;
			BString				fOrigUsername;

			BTextControl*		fTitleControl;
			BTextControl*		fUserControl;
			BTextControl*		fPasswordControl;
			BTextControl*		fUrlControl;
			BTextView*			fNotesControl;
			BTextControl*		fTotpControl;
			BCheckBox*			fShowCheck;
			BStringView*		fTotpCodeLabel;
			BStringView*		fTotpCodeView;
			BMessageRunner*		fTotpRunner;
};


#endif	/* _ENTRY_WINDOW_H */
