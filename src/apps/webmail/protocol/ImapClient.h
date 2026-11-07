/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef WEB_MAIL_IMAP_CLIENT_H
#define WEB_MAIL_IMAP_CLIENT_H


#include "../protocol/MailTypes.h"

#include <String.h>
#include <vector>


struct mailimap;


// Thin, synchronous wrapper over libetpan's IMAP4 driver. One
// instance owns one connection; MailSession drives it from a single
// worker thread. TLS is implicit (port 993) or STARTTLS (143);
// authentication is LOGIN, SASL through libsasl2 or XOAUTH2.
class ImapClient {
public:
	struct Settings {
		BString		host;
		int32		port;			// 0: 993 / 143 by transport
		bool		tls;			// false: STARTTLS
		BString		user;
		BString		password;
		BString		oauthToken;		// XOAUTH2 access token
	};

						ImapClient();
						~ImapClient();

			bool		Connect(const Settings& settings, BString& error);
			void		Disconnect();
			bool		IsConnected() const;

			bool		ListFolders(std::vector<MailFolder>& folders,
								BString& error);
			bool		Select(const char* folder, uint32& exists,
								BString& error);

			// Headers with UID >= firstUid, oldest first, at most
			// `count` of them.
			bool		FetchHeaders(uint32 firstUid, uint32 count,
								std::vector<MailMessageHeader>& headers,
								BString& error);
			bool		FetchMessage(uint32 uid, MailMessage& message,
								BString& error);

			bool		SetSeen(const std::vector<uint32>& uids, bool seen,
								BString& error);
			bool		SetFlagged(const std::vector<uint32>& uids, bool flagged,
								BString& error);
			bool		SetAnswered(const std::vector<uint32>& uids, bool answered,
								BString& error);
			bool		CopyMessages(const char* folder,
								const std::vector<uint32>& uids,
								BString& error);
			bool		MoveMessages(const char* folder,
								const std::vector<uint32>& uids,
								BString& error);
			bool		DeleteMessages(const std::vector<uint32>& uids,
								BString& error);
			bool		Expunge(BString& error);

			// criteria: IMAP search keys, "UNSEEN", "SUBJECT foo", ...
			bool		Search(const char* criteria,
								std::vector<uint32>& uids, BString& error);

			bool		AppendMessage(const char* folder,
								const BString& rfc822, bool seen,
								BString& error);

			// IDLE: Select() first, then IdleStart once; IdleWait()
			// blocks on the connection until traffic or the deadline,
			// IdleDone() leaves IDLE. Reconnect + re-select + re-idle
			// after a failure.
			bool		IdleStart(BString& error);
			bool		IdleWait(int32 seconds, bool& traffic);
			bool		IdleDone(BString& error);

private:
			bool		_StoreFlags(const std::vector<uint32>& uids, bool add,
								const char* flag, BString& error);

		mailimap*		fSession;
		BString			fSelected;
};


#endif // WEB_MAIL_IMAP_CLIENT_H
