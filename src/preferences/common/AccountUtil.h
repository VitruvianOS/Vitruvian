/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _ACCOUNT_UTIL_H
#define _ACCOUNT_UTIL_H


#include <ObjectList.h>
#include <String.h>
#include <SupportDefs.h>


class BMessage;


namespace BPrivate {
namespace Accounts {


// One interactive account (uid >= 1000, interactive shell).
struct Account {
	BString	name;
	BString	realName;
	uid_t	uid;
	bool	isAdmin;
	bool	isSelf;
	bool	hasSession;
};


typedef BObjectList<Account, true> AccountList;
typedef BObjectList<BString, true> StringList;


status_t listAccounts(AccountList* outAccounts);

bool isAdministrator(const char* userName);
bool isReservedName(const char* userName);

// Run a helper through pkexec. args are passed after the helper path;
// password, if non-NULL, is written to the helper's stdin.
status_t runHelper(const char* helper, const StringList* args,
	const char* password, BString& error);

status_t runAdminHelper(const StringList* args, const char* password,
	BString& error);
status_t runUserHelper(const StringList* args, BString& error);


}	// namespace Accounts
}	// namespace BPrivate


#endif	// _ACCOUNT_UTIL_H
