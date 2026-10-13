/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _ACCOUNT_ROSTER_H
#define _ACCOUNT_ROSTER_H


#include <List.h>
#include <Locker.h>
#include <ObjectList.h>
#include <String.h>

#include <Account.h>
#include <AccountProvider.h>


class BLooper;


// Per-process view over ~/config/settings/Accounts, kept current by a node
// monitor. Secrets go through BKeyStore, never here.
class BAccountRoster {
public:
	static	BAccountRoster*	Default();

			status_t			InitCheck() const;

			status_t			GetNextAccount(uint32& cookie,
									BAccount& account);
			status_t			CountAccounts(int32& count) const;
			status_t			FindAccount(const char* id,
									BAccount& account) const;

			status_t			AddAccount(const BAccount& account);
			status_t			UpdateAccount(const BAccount& account);
			status_t			RemoveAccount(const char* id);

			// Provider add-ons, loaded from /system/add-ons/accounts.
			// The roster owns every provider it hands out.
			status_t			GetProvider(BAccountProvider*& provider,
									const char* providerId);
			status_t			GetProviders(
									BObjectList<BAccountProvider>& providers);

			status_t			StartWatching(const BMessenger& observer);
			status_t			StopWatching(const BMessenger& observer);

private:
								BAccountRoster();
								~BAccountRoster();

	static	status_t			_CreateDefault();

			BAccountProvider*	_FindProvider(const char* id) const;
			status_t			_ScanProviders();
			status_t			_Notify(uint32 what, const BAccount* account,
									const char* id);

			friend class AccountsWatchLooper;

			BLocker				fLock;
			status_t			fInitStatus;
			BObjectList<BAccountProvider, true>	fProviders;
			BObjectList<BMessenger, true>		fObservers;
			BLooper*			fWatchLooper;
};


#endif // _ACCOUNT_ROSTER_H
