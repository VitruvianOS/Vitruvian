/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "SipAccounts.h"

#include <Key.h>
#include <KeyStore.h>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <String.h>

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "SipBackend.h"


// Keystore layout: keyring "sip", one password key per account with
// identifier = account id. The "sip" keyring is created on demand.
static const char* kSipKeyring = "sip";


BSipAccounts*
BSipAccounts::Default()
{
	// Deliberately leaked; process-lifetime like the rosters.
	static BSipAccounts* instance = new BSipAccounts();
	return instance;
}


BSipAccounts::BSipAccounts()
{
	Load();
}


BSipAccounts::~BSipAccounts()
{
}


status_t
BSipAccounts::_SettingsPath(const char* id, BString* path)
{
	BPath basePath;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY,
		&basePath);
	if (status != B_OK)
		return status;

	BString directory(basePath.Path());
	directory << "/Accounts/sip";
	status = create_directory(directory.String(), 0755);
	if (status != B_OK && status != B_FILE_EXISTS)
		return status;

	*path = directory;
	*path << "/" << id << ".account";
	return B_OK;
}


status_t
BSipAccounts::_ReadPassword(const char* id, BString* password)
{
	BKeyStore store;
	BPasswordKey key;
	status_t status = store.GetKey(kSipKeyring, B_KEY_TYPE_PASSWORD, "sip",
		id, key);
	if (status != B_OK)
		return status;

	*password = key.Password();
	return B_OK;
}


status_t
BSipAccounts::_WritePassword(const char* id, const char* password)
{
	BKeyStore store;
	store.AddKeyring(kSipKeyring);

	BPasswordKey key(password, B_KEY_PURPOSE_NETWORK, "sip", id);
	return store.AddKey(kSipKeyring, key);
}


status_t
BSipAccounts::AddAccount(const BMessage& settings, const char* password,
	BString* idOut)
{
	const char* uri = NULL;
	if (settings.FindString("sip_uri", &uri) != B_OK || uri[0] == '\0')
		return B_BAD_VALUE;

	// Id derives from the URI so re-adding the same account
	// overwrites instead of duplicating.
	BString id(uri);
	id.ReplaceFirst(":", "_");
	id.ReplaceFirst("/", "_");
	id.ReplaceFirst("@", "_at_");

	BString path;
	status_t status = _SettingsPath(id.String(), &path);
	if (status != B_OK)
		return status;

	BMessage flat = settings;
	flat.AddString("account_id", id.String());
	flat.AddString("account_type", "sip");

	BString directory(path.String());
	int32 slash = directory.FindLast('/');
	if (slash >= 0)
		directory.Truncate(slash);
	create_directory(directory.String(), 0755);

	BFile file;
	status = file.SetTo(path.String(), O_WRONLY | O_CREAT | O_TRUNC);
	if ((status = file.InitCheck()) != B_OK)
		return status;
	if ((status = flat.Flatten(&file)) != B_OK)
		return status;

	if (password != NULL) {
		status = _WritePassword(id.String(), password);
		if (status != B_OK)
			return status;
	}

	// Refresh the cache entry.
	for (size_t i = 0; i < fAccounts.size(); i++) {
		if (fAccounts[i].id == id) {
			fAccounts[i].settings = flat;
			if (idOut != NULL)
				*idOut = id;
			return B_OK;
		}
	}

	Entry entry;
	entry.id = id;
	entry.settings = flat;
	fAccounts.push_back(entry);
	if (idOut != NULL)
		*idOut = id;
	return B_OK;
}


status_t
BSipAccounts::RemoveAccount(const char* id)
{
	if (id == NULL)
		return B_BAD_VALUE;

	bool found = false;
	for (size_t i = 0; i < fAccounts.size(); i++) {
		if (fAccounts[i].id == id) {
			fAccounts.erase(fAccounts.begin() + i);
			found = true;
			break;
		}
	}
	if (!found)
		return B_ENTRY_NOT_FOUND;

	BString path;
	if (_SettingsPath(id, &path) == B_OK)
		unlink(path.String());

	BKeyStore store;
	BKey key(B_KEY_PURPOSE_NETWORK, "sip", id);
	store.RemoveKey(kSipKeyring, key);
	return B_OK;
}


int32
BSipAccounts::CountAccounts()
{
	return (int32)fAccounts.size();
}


status_t
BSipAccounts::AccountAt(int32 index, BMessage* settings, BString* password)
{
	if (index < 0 || index >= (int32)fAccounts.size() || settings == NULL)
		return B_ENTRY_NOT_FOUND;

	*settings = fAccounts[index].settings;
	if (password != NULL)
		_ReadPassword(fAccounts[index].id.String(), password);
	return B_OK;
}


status_t
BSipAccounts::FindAccount(const char* id, BMessage* settings,
	BString* password)
{
	if (id == NULL || settings == NULL)
		return B_BAD_VALUE;

	for (size_t i = 0; i < fAccounts.size(); i++) {
		if (fAccounts[i].id == id) {
			*settings = fAccounts[i].settings;
			if (password != NULL)
				_ReadPassword(id, password);
			return B_OK;
		}
	}
	return B_ENTRY_NOT_FOUND;
}


void
BSipAccounts::Load()
{
	fAccounts.clear();

	BPath basePath;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &basePath) != B_OK)
		return;

	BString directory(basePath.Path());
	directory << "/Accounts/sip";

	BDirectory dir(directory.String());
	if (dir.InitCheck() != B_OK)
		return;

	BEntry entry;
	while (dir.GetNextEntry(&entry) == B_OK) {
		BPath entryPath;
		if (entry.GetPath(&entryPath) != B_OK)
			continue;

		const char* name = entryPath.Leaf();
		size_t length = strlen(name);
		if (length < 8 || strcmp(name + length - 8, ".account") != 0)
			continue;

		BFile file(&entry, B_READ_ONLY);
		if (file.InitCheck() != B_OK)
			continue;

		BMessage settings;
		if (settings.Unflatten(&file) != B_OK)
			continue;

		BString id;
		if (settings.FindString("account_id", &id) != B_OK)
			continue;

		Entry stored;
		stored.id = id;
		stored.settings = settings;
		fAccounts.push_back(stored);
	}
}


status_t
BSipAccounts::LoadIntoBackend(void* sipBackend)
{
	if (sipBackend == NULL)
		return B_BAD_VALUE;

	SipBackend* backend = (SipBackend*)sipBackend;
	for (size_t i = 0; i < fAccounts.size(); i++) {
		BString password;
		_ReadPassword(fAccounts[i].id.String(), &password);
		backend->AddAccount(fAccounts[i].settings, password.String());
	}
	return B_OK;
}
