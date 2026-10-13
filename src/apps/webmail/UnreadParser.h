/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef WEB_MAIL_UNREAD_PARSER_H
#define WEB_MAIL_UNREAD_PARSER_H


#include <SupportDefs.h>


// Parses the unread count from a page title, leading "(3) Inbox" or trailing
// "Inbox (3)". Returns 0 when there is none.
int32	ParseUnreadCount(const char* title);


#endif // WEB_MAIL_UNREAD_PARSER_H
