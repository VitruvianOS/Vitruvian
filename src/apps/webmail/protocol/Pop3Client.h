/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef WEB_MAIL_POP3_CLIENT_H
#define WEB_MAIL_POP3_CLIENT_H


#include "../protocol/MailTypes.h"

#include <String.h>
#include <vector>


struct mailpop3;


// One POP3 mailbox: UIDL-keyed download, optional deletion.
class Pop3Client {
public:
	struct Settings {
		BString		host;
		int32		port;			// 0: 995 / 110 by transport
		bool		tls;			// false: STARTTLS
		BString		user;
		BString		password;
	};

	struct ServerMessage {
		BString		uidl;
		uint32		size;
		unsigned int index;			// 1-based on the server
	};

						Pop3Client();
						~Pop3Client();

			bool		Connect(const Settings& settings, BString& error);
			void		Disconnect();

			// Lists the server with UIDLs; the local store keys its
			// cache on the UIDL strings.
			bool		ListMessages(std::vector<ServerMessage>& messages,
								BString& error);
			// index is the 1-based position from ListMessages.
			bool		FetchMessage(unsigned int index, BString& rfc822,
								BString& error);
			bool		DeleteMessage(unsigned int index, BString& error);

private:
		mailpop3*		fSession;
};


#endif // WEB_MAIL_POP3_CLIENT_H
