/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <AccountRoster.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <Autolock.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Looper.h>
#include <NodeMonitor.h>
#include <Path.h>

#include <AccountsDefs.h>


static const char* kAccountsFolderName = "Accounts";
static const char* kProviderAddonDirectory = "/system/add-ons/accounts";


static status_t
_AccountsPath(BPath& path)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;

	status = path.Append(kAccountsFolderName);
	if (status != B_OK)
		return status;

	if (create_directory(path.Path(), 0700) != B_OK
		&& !BEntry(path.Path()).Exists())
		return B_ERROR;

	return B_OK;
}


static status_t
_LoadAccount(const BPath& path, BAccount& account)
{
	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return file.InitCheck();

	BMessage archive;
	status_t status = archive.Unflatten(&file);
	if (status != B_OK)
		return status;

	account = BAccount(archive);
	if (account.Id()[0] == '\0')
		return B_ERROR;

	return B_OK;
}


static status_t
_SaveAccountFile(const BAccount& account)
{
	BPath path;
	status_t status = _AccountsPath(path);
	if (status != B_OK)
		return status;

	status = path.Append(account.Id());
	if (status != B_OK)
		return status;

	BMessage archive;
	status = account.Archive(&archive);
	if (status != B_OK)
		return status;

	BFile file(path.Path(), B_READ_ONLY | B_WRITE_ONLY | B_CREATE_FILE);
	status = file.InitCheck();
	if (status != B_OK)
		return status;

	status = archive.Flatten(&file);
	if (status != B_OK)
		return status;

	file.SetPermissions(0600);
	return B_OK;
}


static status_t
_RemoveAccountFile(const char* id)
{
	if (id == NULL || id[0] == '\0')
		return B_BAD_VALUE;

	BPath path;
	status_t status = _AccountsPath(path);
	if (status != B_OK)
		return status;

	status = path.Append(id);
	if (status != B_OK)
		return status;

	BEntry entry(path.Path());
	if (!entry.Exists())
		return B_ENTRY_NOT_FOUND;

	return entry.Remove();
}


static int
_CompareNames(const BString* first, const BString* second)
{
	return first->Compare(second->String());
}


static status_t
_AccountNames(BObjectList<BString, true>& names)
{
	BPath path;
	status_t status = _AccountsPath(path);
	if (status != B_OK)
		return status;

	BDirectory directory(path.Path());
	status = directory.InitCheck();
	if (status != B_OK)
		return status;

	BEntry entry;
	while (directory.GetNextEntry(&entry) == B_OK) {
		if (entry.IsDirectory())
			continue;

		BPath entryPath;
		if (entry.GetPath(&entryPath) != B_OK)
			continue;

		names.AddItem(new BString(entryPath.Leaf()));
	}

	names.SortItems(&_CompareNames);
	return B_OK;
}


static void
_NotifyObserver(BMessenger* observer, uint32 what, const BAccount* account,
	const char* id)
{
	BMessage message(what);
	if (account != NULL) {
		message.AddString(kAccountsIdField, account->Id());
		message.AddString(kAccountsProviderField, account->ProviderId());
		message.AddString(kAccountsLabelField, account->Label());
	} else if (id != NULL)
		message.AddString(kAccountsIdField, id);

	observer->SendMessage(&message);
}


class AccountsWatchLooper : public BLooper {
public:
								AccountsWatchLooper(BAccountRoster* roster,
									const BPath& directory);

	virtual	void				DispatchMessage(BMessage* message,
									BHandler* target);

private:
			BAccountRoster*		fRoster;
			node_ref			fDirectoryRef;
};


AccountsWatchLooper::AccountsWatchLooper(BAccountRoster* roster,
	const BPath& directory)
	:
	BLooper("accounts watch"),
	fRoster(roster)
{
	BEntry entry(directory.Path());
	if (entry.GetNodeRef(&fDirectoryRef) != B_OK)
		return;

	Run();
	watch_node(&fDirectoryRef, B_WATCH_NAME | B_WATCH_STAT, this);
}


void
AccountsWatchLooper::DispatchMessage(BMessage* message, BHandler* target)
{
	switch (message->what) {
		case B_ENTRY_CREATED:
		case B_ENTRY_MOVED:
		case B_STAT_CHANGED:
		{
			const char* name = NULL;
			if (message->FindString("be:name", &name) != B_OK)
				break;

			BAccount account;
			BPath path;
			if (_AccountsPath(path) != B_OK
				|| path.Append(name) != B_OK
				|| _LoadAccount(path, account) != B_OK)
				break;

			uint32 what = message->what == B_ENTRY_CREATED
				? kAccountAdded : kAccountChanged;
			fRoster->_Notify(what, &account, NULL);
			break;
		}

		case B_ENTRY_REMOVED:
		{
			const char* name = NULL;
			if (message->FindString("be:name", &name) != B_OK)
				break;

			fRoster->_Notify(kAccountRemoved, NULL, name);
			break;
		}

		default:
			BLooper::DispatchMessage(message, target);
			break;
	}
}


static BAccountRoster* sDefaultRoster = NULL;
static BLocker sDefaultLock;


BAccountRoster*
BAccountRoster::Default()
{
	BAutolock lock(sDefaultLock);
	if (sDefaultRoster == NULL) {
		if (_CreateDefault() != B_OK)
			return NULL;
	}

	return sDefaultRoster;
}


status_t
BAccountRoster::_CreateDefault()
{
	BAccountRoster* roster = new BAccountRoster();
	if (roster->InitCheck() != B_OK) {
		delete roster;
		return B_ERROR;
	}

	sDefaultRoster = roster;
	return B_OK;
}


BAccountRoster::BAccountRoster()
	:
	fInitStatus(B_NO_INIT),
	fWatchLooper(NULL)
{
	BPath path;
	fInitStatus = _AccountsPath(path);
	if (fInitStatus != B_OK)
		return;

	fWatchLooper = new AccountsWatchLooper(this, path);
	fInitStatus = B_OK;
}


BAccountRoster::~BAccountRoster()
{
	if (fWatchLooper != NULL) {
		fWatchLooper->PostMessage(B_QUIT_REQUESTED);
		fWatchLooper = NULL;
	}
}


status_t
BAccountRoster::InitCheck() const
{
	return fInitStatus;
}


status_t
BAccountRoster::GetNextAccount(uint32& cookie, BAccount& account)
{
	BAutolock lock(fLock);
	if (fInitStatus != B_OK)
		return fInitStatus;

	BObjectList<BString, true> names;
	status_t status = _AccountNames(names);
	if (status != B_OK)
		return status;

	if ((int32)cookie >= names.CountItems())
		return B_ENTRY_NOT_FOUND;

	BPath path;
	status = _AccountsPath(path);
	if (status != B_OK)
		return status;

	status = path.Append(names.ItemAt((int32)cookie)->String());
	if (status != B_OK)
		return status;

	cookie++;
	return _LoadAccount(path, account);
}


status_t
BAccountRoster::CountAccounts(int32& count) const
{
	count = 0;

	BAccountRoster* self = const_cast<BAccountRoster*>(this);
	BAutolock lock(self->fLock);
	if (fInitStatus != B_OK)
		return fInitStatus;

	BObjectList<BString, true> names;
	status_t status = _AccountNames(names);
	if (status != B_OK)
		return status;

	count = names.CountItems();
	return B_OK;
}


status_t
BAccountRoster::FindAccount(const char* id, BAccount& account) const
{
	if (id == NULL || id[0] == '\0')
		return B_BAD_VALUE;

	BAccountRoster* self = const_cast<BAccountRoster*>(this);
	BAutolock lock(self->fLock);
	if (fInitStatus != B_OK)
		return fInitStatus;

	BPath path;
	status_t status = _AccountsPath(path);
	if (status != B_OK)
		return status;

	status = path.Append(id);
	if (status != B_OK)
		return status;

	if (!BEntry(path.Path()).Exists())
		return B_ENTRY_NOT_FOUND;

	return _LoadAccount(path, account);
}


status_t
BAccountRoster::AddAccount(const BAccount& account)
{
	BAutolock lock(fLock);
	if (fInitStatus != B_OK)
		return fInitStatus;

	if (account.Id()[0] == '\0' || account.ProviderId()[0] == '\0')
		return B_BAD_VALUE;

	BAccount existing;
	if (FindAccount(account.Id(), existing) == B_OK)
		return B_FILE_EXISTS;

	return _SaveAccountFile(account);
}


status_t
BAccountRoster::UpdateAccount(const BAccount& account)
{
	BAutolock lock(fLock);
	if (fInitStatus != B_OK)
		return fInitStatus;

	if (account.Id()[0] == '\0')
		return B_BAD_VALUE;

	BAccount existing;
	if (FindAccount(account.Id(), existing) != B_OK)
		return B_ENTRY_NOT_FOUND;

	return _SaveAccountFile(account);
}


status_t
BAccountRoster::RemoveAccount(const char* id)
{
	BAutolock lock(fLock);
	if (fInitStatus != B_OK)
		return fInitStatus;

	return _RemoveAccountFile(id);
}


BAccountProvider*
BAccountRoster::_FindProvider(const char* id) const
{
	for (int32 i = 0; i < fProviders.CountItems(); i++) {
		BAccountProvider* provider = fProviders.ItemAt(i);
		if (strcmp(provider->Id(), id) == 0)
			return provider;
	}

	return NULL;
}


status_t
BAccountRoster::_ScanProviders()
{
	// Caller holds fLock.
	BDirectory directory(kProviderAddonDirectory);
	if (directory.InitCheck() != B_OK)
		return directory.InitCheck();

	BEntry entry;
	while (directory.GetNextEntry(&entry) == B_OK) {
		if (entry.IsDirectory())
			continue;

		BPath path;
		if (entry.GetPath(&path) != B_OK)
			continue;

		void* image = dlopen(path.Path(), RTLD_NOW);
		if (image == NULL)
			continue;

		instantiate_account_provider_func factory
			= (instantiate_account_provider_func)dlsym(image,
				"instantiate_account_provider");
		if (factory == NULL) {
			dlclose(image);
			continue;
		}

		BAccountProvider* provider = factory();
		if (provider == NULL) {
			dlclose(image);
			continue;
		}

		if (_FindProvider(provider->Id()) == NULL)
			fProviders.AddItem(provider);
		else
			delete provider;

		// Keep the image resident for the provider's lifetime.
	}

	return B_OK;
}


status_t
BAccountRoster::GetProvider(BAccountProvider*& provider, const char* providerId)
{
	if (providerId == NULL || providerId[0] == '\0')
		return B_BAD_VALUE;

	BAutolock lock(fLock);

	provider = _FindProvider(providerId);
	if (provider != NULL)
		return B_OK;

	_ScanProviders();

	provider = _FindProvider(providerId);
	if (provider != NULL)
		return B_OK;

	return B_ENTRY_NOT_FOUND;
}


status_t
BAccountRoster::GetProviders(BObjectList<BAccountProvider>& providers)
{
	BAutolock lock(fLock);

	_ScanProviders();

	for (int32 i = 0; i < fProviders.CountItems(); i++)
		providers.AddItem(fProviders.ItemAt(i));

	return B_OK;
}


status_t
BAccountRoster::StartWatching(const BMessenger& observer)
{
	BAutolock lock(fLock);
	if (fInitStatus != B_OK)
		return fInitStatus;

	if (!observer.IsValid())
		return B_BAD_VALUE;

	for (int32 i = 0; i < fObservers.CountItems(); i++) {
		if (*fObservers.ItemAt(i) == observer)
			return B_OK;
	}

	fObservers.AddItem(new BMessenger(observer));
	return B_OK;
}


status_t
BAccountRoster::StopWatching(const BMessenger& observer)
{
	BAutolock lock(fLock);

	for (int32 i = 0; i < fObservers.CountItems(); i++) {
		if (*fObservers.ItemAt(i) == observer) {
			delete fObservers.RemoveItemAt(i);
			return B_OK;
		}
	}

	return B_BAD_VALUE;
}


status_t
BAccountRoster::_Notify(uint32 what, const BAccount* account, const char* id)
{
	BAutolock lock(fLock);

	for (int32 i = 0; i < fObservers.CountItems(); i++) {
		_NotifyObserver(fObservers.ItemAt(i), what, account, id);
	}

	return B_OK;
}
