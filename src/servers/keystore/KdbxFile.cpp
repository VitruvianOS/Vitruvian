/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "KdbxFile.h"

#include "KdbxCrypto.h"

#include <Entry.h>
#include <File.h>
#include <Path.h>
#include <String.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


KdbxFile::KdbxFile()
{
}


KdbxFile::~KdbxFile()
{
}


static bool
_IsTotpKey(const BString& identifier)
{
	return identifier.EndsWith("/totp");
}


status_t
KdbxFile::_BuildFromKeyStore(const char* keyring, const char* password,
	uint8** _data, size_t* _length)
{
	BKeyStore keyStore;
	KdbxDatabase* database = kdbx_DatabaseNew(keyring);
	if (database == NULL)
		return B_NO_MEMORY;

	uint32 cookie = 0;
	BPasswordKey key;
	while (true) {
		status_t result = keyStore.GetNextKey(keyring, B_KEY_TYPE_ANY,
			B_KEY_PURPOSE_ANY, cookie, key);
		if (result == B_ENTRY_NOT_FOUND)
			break;
		if (result != B_OK) {
			kdbx_DatabaseFree(database);
			return result;
		}

		BString identifier = key.Identifier();
		if (_IsTotpKey(identifier))
			continue;

		BString title = identifier;
		BString username = key.SecondaryIdentifier();
		BString passwordText;
		if (key.DataLength() > 0 && key.Data() != NULL)
			passwordText.SetTo((const char*)key.Data());

		BString totp;
		BPasswordKey totpKey;
		BString totpIdentifier;
		totpIdentifier << identifier << "/totp";
		if (keyStore.GetKey(keyring, B_KEY_TYPE_PASSWORD,
				totpIdentifier.String(), username.String(), totpKey) == B_OK
			&& totpKey.DataLength() > 0)
			totp.SetTo((const char*)totpKey.Data());

		if (kdbx_DatabaseAddEntry(database, title.String(),
				username.String(), passwordText.String(), "", "",
				totp.Length() > 0 ? totp.String() : NULL) != 0) {
			kdbx_DatabaseFree(database);
			return B_NO_MEMORY;
		}
	}

	int status = kdbx_Write(database, password, _data, _length);
	kdbx_DatabaseFree(database);
	return status == 0 ? B_OK : B_ERROR;
}


status_t
KdbxFile::ExportToPath(const char* path, const char* password,
	const char* keyring)
{
	if (path == NULL || password == NULL)
		return B_BAD_VALUE;

	uint8* data = NULL;
	size_t length = 0;
	status_t result = _BuildFromKeyStore(keyring, password, &data, &length);
	if (result != B_OK)
		return result;

	BFile file;
	result = file.SetTo(path, B_WRITE_ONLY | B_CREATE_FILE);
	if (result == B_OK)
		result = file.SetSize(0);
	if (result != B_OK) {
		free(data);
		return result;
	}

	ssize_t written = file.Write(data, length);
	memset(data, 0, length);
	free(data);
	return written == (ssize_t)length ? B_OK : B_ERROR;
}


status_t
KdbxFile::_WriteToKeyStore(const uint8* data, size_t length,
	const char* password, const char* keyring, bool overwriteExisting)
{
	KdbxDatabase* database = kdbx_Read(data, length, password);
	if (database == NULL)
		return B_BAD_VALUE;

	BKeyStore keyStore;
	for (KdbxEntry* entry = database->entries; entry != NULL;
			entry = entry->next) {
		BString title = entry->title != NULL ? entry->title : "";
		BString username = entry->username != NULL ? entry->username : "";
		if (title.Length() == 0)
			continue;

		if (!overwriteExisting) {
			BPasswordKey existing;
			if (keyStore.GetKey(keyring, B_KEY_TYPE_PASSWORD, title.String(),
					username.String(), existing) == B_OK) {
				continue;
			}
		} else {
			BPasswordKey existing;
			if (keyStore.GetKey(keyring, B_KEY_TYPE_PASSWORD, title.String(),
					username.String(), existing) == B_OK)
				keyStore.RemoveKey(existing);
		}

		const char* passwordText =
			entry->password != NULL ? entry->password : "";
		BPasswordKey passwordKey(passwordText, B_KEY_PURPOSE_GENERIC,
			title.String(), username.String());
		status_t result = keyStore.AddKey(keyring, passwordKey);
		if (result != B_OK) {
			kdbx_DatabaseFree(database);
			return result;
		}

		if (entry->totp != NULL && entry->totp[0] != '\0') {
			BString totpIdentifier;
			totpIdentifier << title << "/totp";
			BPasswordKey totpKey(entry->totp, B_KEY_PURPOSE_GENERIC,
				totpIdentifier.String(), username.String());
			keyStore.AddKey(keyring, totpKey);
		}
	}

	kdbx_DatabaseFree(database);
	return B_OK;
}


status_t
KdbxFile::ImportFromPath(const char* path, const char* password,
	const char* keyring, bool overwriteExisting)
{
	if (path == NULL || password == NULL)
		return B_BAD_VALUE;

	BFile file;
	status_t result = file.SetTo(path, B_READ_ONLY);
	if (result != B_OK)
		return result;

	off_t size = 0;
	if (file.GetSize(&size) != B_OK || size <= 0 || size > 64 * 1024 * 1024)
		return B_BAD_VALUE;

	uint8* data = (uint8_t*)malloc((size_t)size);
	if (data == NULL)
		return B_NO_MEMORY;
	if (file.Read(data, size) != size) {
		free(data);
		return B_ERROR;
	}

	result = _WriteToKeyStore(data, (size_t)size, password, keyring,
		overwriteExisting);
	memset(data, 0, (size_t)size);
	free(data);
	return result;
}
