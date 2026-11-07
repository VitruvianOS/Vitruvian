/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "Pop3Client.h"

extern "C" {
#include <libetpan/mailpop3.h>
#include <libetpan/mailpop3_ssl.h>
#include <libetpan/mailpop3_socket.h>
#include <libetpan/carray.h>
}

#include <cstring>
#include <vector>


Pop3Client::Pop3Client()
	:
	fSession(NULL)
{
}


Pop3Client::~Pop3Client()
{
	Disconnect();
}


bool
Pop3Client::Connect(const Settings& settings, BString& error)
{
	Disconnect();

	fSession = mailpop3_new(0, NULL);
	if (fSession == NULL) {
		error = "out of memory";
		return false;
	}

	uint16_t port = (uint16_t)(settings.port > 0 ? settings.port
		: (settings.tls ? 995 : 110));
	int result;
	if (settings.tls) {
		result = mailpop3_ssl_connect(fSession, settings.host.String(),
			port);
	} else {
		result = mailpop3_socket_connect(fSession, settings.host.String(),
			port);
		if (result == MAILPOP3_NO_ERROR)
			result = mailpop3_stls(fSession);
	}
	if (result != MAILPOP3_NO_ERROR) {
		error = BString("cannot reach ") << settings.host << ":" << port;
		Disconnect();
		return false;
	}

	// APOP when the server offers it, USER/PASS otherwise; SASL
	// (CRAM-MD5 and friends) through mailpop3_auth is the fallback
	// for servers that require it.
	result = mailpop3_apop(fSession, settings.user.String(),
		settings.password.String());
	if (result != MAILPOP3_NO_ERROR)
		result = mailpop3_user(fSession, settings.user.String());
	if (result == MAILPOP3_NO_ERROR)
		result = mailpop3_pass(fSession, settings.password.String());
	if (result != MAILPOP3_NO_ERROR) {
		result = mailpop3_auth(fSession, "CRAM-MD5", NULL, NULL, NULL,
			settings.user.String(), settings.user.String(),
			settings.password.String(), NULL);
	}
	if (result != MAILPOP3_NO_ERROR) {
		error = "POP3 authentication failed";
		Disconnect();
		return false;
	}
	return true;
}


void
Pop3Client::Disconnect()
{
	if (fSession == NULL)
		return;
	mailpop3_quit(fSession);
	mailpop3_free(fSession);
	fSession = NULL;
}


bool
Pop3Client::ListMessages(std::vector<ServerMessage>& messages, BString& error)
{
	if (fSession == NULL) {
		error = "not connected";
		return false;
	}

	carray* list = NULL;
	if (mailpop3_list(fSession, &list) != MAILPOP3_NO_ERROR || list == NULL) {
		error = "LIST failed";
		return false;
	}
	unsigned int count = carray_count(list);
	for (unsigned int i = 0; i < count; i++) {
		mailpop3_msg_info* info
			= (mailpop3_msg_info*)carray_get(list, i);
		if (info == NULL)
			continue;
		ServerMessage message;
		message.uidl = info->msg_uidl != NULL ? info->msg_uidl : "";
		if (message.uidl.Length() == 0)
			message.uidl = BString() << info->msg_index;
		message.size = info->msg_size;
		message.index = info->msg_index;
		messages.push_back(message);
	}
	return true;
}


bool
Pop3Client::FetchMessage(unsigned int index, BString& rfc822, BString& error)
{
	if (fSession == NULL) {
		error = "not connected";
		return false;
	}
	char* content = NULL;
	size_t length = 0;
	if (mailpop3_retr(fSession, index, &content, &length)
		!= MAILPOP3_NO_ERROR) {
		error = "RETR failed";
		return false;
	}
	rfc822.SetTo(content, length);
	mailpop3_retr_free(content);
	return true;
}


bool
Pop3Client::DeleteMessage(unsigned int index, BString& error)
{
	if (fSession == NULL) {
		error = "not connected";
		return false;
	}
	if (mailpop3_dele(fSession, index) != MAILPOP3_NO_ERROR) {
		error = "DELE failed";
		return false;
	}
	return true;
}
