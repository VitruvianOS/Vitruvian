/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "SmtpClient.h"

extern "C" {
#include <libetpan/mailsmtp.h>
#include <libetpan/mailsmtp_ssl.h>
#include <libetpan/mailsmtp_socket.h>
}

#include <cstring>
#include <vector>


SmtpClient::SmtpClient()
	:
	fSession(NULL)
{
}


SmtpClient::~SmtpClient()
{
	Disconnect();
}


bool
SmtpClient::Connect(const Settings& settings, BString& error)
{
	Disconnect();

	fSession = mailsmtp_new(0, NULL);
	if (fSession == NULL) {
		error = "out of memory";
		return false;
	}

	uint16_t port = (uint16_t)(settings.port > 0 ? settings.port
		: (settings.tls ? 465 : 587));
	int result;
	if (settings.tls) {
		result = mailsmtp_ssl_connect(fSession, settings.host.String(),
			port);
	} else {
		result = mailsmtp_socket_connect(fSession, settings.host.String(),
			port);
		if (result == MAILSMTP_NO_ERROR)
			result = mailsmtp_socket_starttls(fSession);
	}
	if (result != MAILSMTP_NO_ERROR) {
		error = BString("cannot reach ") << settings.host << ":" << port;
		Disconnect();
		return false;
	}

	if (settings.oauthToken.Length() > 0) {
		result = mailsmtp_oauth2_authenticate(fSession,
			settings.user.String(), settings.oauthToken.String());
	} else {
		result = mailsmtp_auth(fSession, settings.user.String(),
			settings.password.String());
	}
	if (result != MAILSMTP_NO_ERROR) {
		error = "SMTP authentication failed";
		Disconnect();
		return false;
	}
	return true;
}


void
SmtpClient::Disconnect()
{
	if (fSession == NULL)
		return;
	mailsmtp_quit(fSession);
	mailsmtp_free(fSession);
	fSession = NULL;
}


bool
SmtpClient::Send(const MailAddress& from,
	const std::vector<MailAddress>& recipients, const BString& rfc822,
	BString& error)
{
	if (fSession == NULL) {
		error = "not connected";
		return false;
	}
	if (recipients.size() == 0) {
		error = "no recipients";
		return false;
	}

	if (mailsmtp_mail(fSession, from.address.String())
		!= MAILSMTP_NO_ERROR) {
		error = "MAIL FROM rejected";
		return false;
	}
	for (size_t i = 0; i < recipients.size(); i++) {
		if (mailsmtp_rcpt(fSession, recipients[i].address.String())
			!= MAILSMTP_NO_ERROR) {
			error = BString("RCPT TO rejected for ")
				<< recipients[i].address;
			return false;
		}
	}
	if (mailsmtp_data(fSession) != MAILSMTP_NO_ERROR) {
		error = "DATA rejected";
		return false;
	}
	if (mailsmtp_data_message(fSession, rfc822.String(), rfc822.Length())
		!= MAILSMTP_NO_ERROR) {
		error = "message rejected";
		return false;
	}
	return true;
}
