/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _ACCOUNTS_DEFS_H
#define _ACCOUNTS_DEFS_H


#include <SupportDefs.h>


// Roster notifications, delivered to observers registered with
// BAccountRoster::StartWatching(). Fields: "id", "provider", "label".
enum {
	kAccountAdded				= 'ACad',
	kAccountRemoved				= 'ACrm',
	kAccountChanged				= 'ACch'
};

// Reply to BAccount::GetTokenAsync(). Fields: "status", and on success
// "token" plus "expiry" (absolute time in usecs since epoch, int64).
enum {
	kAccountTokenResult			= 'ACtk'
};

// Posted by a provider add-on to the consent messenger before an
// interactive sign-in. Field: "url" (the page to open in a browser).
enum {
	kAccountConsentRequest		= 'ACns'
};

// Posted by a provider add-on when its setup window finishes. Field:
// "status". The account has already been added to or updated in the roster.
enum {
	kAccountEditResult			= 'ACed'
};

// Message field names shared between the kit and the add-ons.
extern const char* kAccountsIdField;
extern const char* kAccountsProviderField;
extern const char* kAccountsLabelField;
extern const char* kAccountsUserField;
extern const char* kAccountsStatusField;
extern const char* kAccountsReasonField;
extern const char* kAccountsTokenField;
extern const char* kAccountsUrlField;
extern const char* kAccountsExpiryField;

// Account capabilities, declared by the provider add-on.
enum {
	kAccountCapabilityMail		= 1 << 0,
	kAccountCapabilityCalendar	= 1 << 1,
	kAccountCapabilityContacts	= 1 << 2,
	kAccountCapabilityFiles		= 1 << 3
};


#endif // _ACCOUNTS_DEFS_H
