/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef WEB_MAIL_SMTP_CLIENT_H
#define WEB_MAIL_SMTP_CLIENT_H


#include "../protocol/MailTypes.h"

#include <String.h>
#include <vector>


struct mailsmtp;


// Synchronous SMTP submission over libetpan: implicit TLS (465) or
// STARTTLS (587), password or XOAUTH2 authentication.
class SmtpClient {
public:
	struct Settings {
		BString		host;
		int32		port;			// 0: 465 / 587 by transport
		bool		tls;			// false: STARTTLS
		BString		user;
		BString		password;
		BString		oauthToken;
	};

						SmtpClient();
						~SmtpClient();

			bool		Connect(const Settings& settings, BString& error);
			void		Disconnect();

			// Sends one RFC822 message; envelope recipients are the
			// To, Cc and Bcc lists.
			bool		Send(const MailAddress& from,
								const std::vector<MailAddress>& recipients,
								const BString& rfc822, BString& error);

private:
		mailsmtp*		fSession;
};


#endif // WEB_MAIL_SMTP_CLIENT_H
