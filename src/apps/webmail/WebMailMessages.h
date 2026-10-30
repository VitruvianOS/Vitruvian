/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef WEB_MAIL_MESSAGES_H
#define WEB_MAIL_MESSAGES_H


#include <Message.h>
#include <SupportDefs.h>


// Window messages
enum {
	MSG_ACCOUNT_SELECTED		= 'WmAs',
	MSG_ACCOUNTS_CHANGED		= 'WmAc',
	MSG_ADD_ACCOUNT				= 'WmAd',
	MSG_REMOVE_ACCOUNT			= 'WmRm',
	MSG_ACCOUNT_UNREAD			= 'WmUn',
	MSG_NEW_MAIL				= 'WmNm',
	MSG_OPEN_MAILTO				= 'WmMt',
	MSG_NATIVE_EVENT			= 'WmNt',
	MSG_OAUTH_FINISHED			= 'WmOa',
	MSG_COMPOSE_REQUESTED		= 'WmCp',
	MSG_SETTINGS_CLOSED			= 'WmSc'
};


// Field names
#define WEBMAIL_ACCOUNT_ID_FIELD	"webmail:account"
#define WEBMAIL_UNREAD_FIELD		"webmail:unread"
#define WEBMAIL_SUBJECT_FIELD		"webmail:subject"
#define WEBMAIL_FROM_FIELD			"webmail:from"
#define WEBMAIL_URL_FIELD			"webmail:url"
#define WEBMAIL_CODE_FIELD			"webmail:code"
#define WEBMAIL_ERROR_FIELD			"webmail:error"


#endif // WEB_MAIL_MESSAGES_H
