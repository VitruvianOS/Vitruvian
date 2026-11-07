/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _ACCOUNTS_PREFLET_H
#define _ACCOUNTS_PREFLET_H


// Launching the Accounts preflet from other applications.
//
// Command line: Accounts --add mail
// Message: be_roster->Launch(kAccountsPreferencesSignature, &message) with
// message.what = kMsgAccountsAdd and the string field kAccountsTypeField
// set to a type below. A running preflet is brought to front and opens
// the editor.

static const char* const kAccountsPreferencesSignature
	= "application/x-vnd.vos-Accounts";

enum {
	kMsgAccountsAdd			= 'AcAd'
};

static const char* const kAccountsTypeField = "type";
static const char* const kAccountTypeMail = "mail";


#endif // _ACCOUNTS_PREFLET_H
