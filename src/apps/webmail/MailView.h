/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef WEB_MAIL_VIEW_H
#define WEB_MAIL_VIEW_H


#include "Accounts.h"
#include "protocol/MailSession.h"

#include <String.h>
#include <View.h>
#include <WebView.h>


// The content area for one account: a BWebView showing either the
// provider's own webmail (per-account persistent session, unread
// badge parsing, mailto interception) or the bundled native UI over
// vos-mail: talking to the IMAP/POP3/SMTP session underneath.
class MailView : public BView {
public:
						MailView(const Account& account);

	virtual				~MailView();

	virtual void		AttachedToWindow();
	virtual void		AllAttached();
	virtual void		FrameResized(float width, float height);

			const Account&	GetAccount() const { return fAccount; }

			// Opens the compose flow: the provider's compose URL for
			// web accounts, the compose page for native ones.
			void		Compose(const BString& to = BString(),
								const BString& subject = BString(),
								const BString& body = BString());
			// "check for mail" from the menu.
			void		CheckMail();
			// Routes a B_WEB_SCRIPT_MESSAGE from the window.
			void		HandleScriptMessage(BMessage* message);
			// Routes a MSG_NATIVE_EVENT from the window.
			void		HandleNativeEvent(BMessage* message);

			// Unread count pushed by the title/badge parsing.
			void		SetUnread(int32 unread);
			int32		Unread() const { return fUnread; }

			// Hands an attachment path (from the file panel) to the
			// compose form.
			void		AttachPath(const char* path);

private:
			void		_Load();
			void		_InjectBadgeScript();
			void		_DispatchToPage(const BString& json);

		Account			fAccount;
	BWebView*			fWebView;
	MailSession*		fSession;
			int32		fUnread;
			bool		fNative;
			BString		fCurrentFolder;
};


#endif // WEB_MAIL_VIEW_H
