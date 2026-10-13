/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <Account.h>

#include <string.h>

#include <Key.h>
#include <KeyStore.h>
#include <Messenger.h>
#include <OS.h>

#include "AccountRequest.h"


static const char* kKeyringName = "Accounts";
static const char* kPasswordSecondaryId = "password";


BAccount::BAccount()
	:
	fCapabilities(0),
	fCreationTime(0),
	fModificationTime(0)
{
}


BAccount::BAccount(const BAccount& other)
	:
	fId(other.fId),
	fProviderId(other.fProviderId),
	fLabel(other.fLabel),
	fUserName(other.fUserName),
	fCapabilities(other.fCapabilities),
	fCreationTime(other.fCreationTime),
	fModificationTime(other.fModificationTime),
	fSettings(other.fSettings)
{
}


BAccount::BAccount(const BMessage& archive)
	:
	fCapabilities(0),
	fCreationTime(0),
	fModificationTime(0)
{
	const char* value = NULL;
	if (archive.FindString(kAccountsIdField, &value) == B_OK)
		fId = value;
	if (archive.FindString(kAccountsProviderField, &value) == B_OK)
		fProviderId = value;
	if (archive.FindString(kAccountsLabelField, &value) == B_OK)
		fLabel = value;
	if (archive.FindString(kAccountsUserField, &value) == B_OK)
		fUserName = value;

	archive.FindUInt32("capabilities", &fCapabilities);
	archive.FindInt64("created", &fCreationTime);
	archive.FindInt64("modified", &fModificationTime);

	BMessage settings;
	if (archive.FindMessage("settings", &settings) == B_OK)
		fSettings = settings;
}


BAccount::~BAccount()
{
}


status_t
BAccount::Archive(BMessage* archive) const
{
	if (archive == NULL)
		return B_BAD_VALUE;

	archive->AddString(kAccountsIdField, fId.String());
	archive->AddString(kAccountsProviderField, fProviderId.String());
	archive->AddString(kAccountsLabelField, fLabel.String());
	archive->AddString(kAccountsUserField, fUserName.String());
	archive->AddUInt32("capabilities", fCapabilities);
	archive->AddInt64("created", fCreationTime);
	archive->AddInt64("modified", fModificationTime);
	archive->AddMessage("settings", &fSettings);
	return B_OK;
}


const char*
BAccount::Id() const
{
	return fId.String();
}


const char*
BAccount::ProviderId() const
{
	return fProviderId.String();
}


const char*
BAccount::Label() const
{
	return fLabel.String();
}


const char*
BAccount::UserName() const
{
	return fUserName.String();
}


uint32
BAccount::Capabilities() const
{
	return fCapabilities;
}


bigtime_t
BAccount::CreationTime() const
{
	return fCreationTime;
}


bigtime_t
BAccount::ModificationTime() const
{
	return fModificationTime;
}


status_t
BAccount::SetId(const char* id)
{
	if (id == NULL || id[0] == '\0')
		return B_BAD_VALUE;

	fId = id;
	return B_OK;
}


status_t
BAccount::SetProviderId(const char* providerId)
{
	if (providerId == NULL || providerId[0] == '\0')
		return B_BAD_VALUE;

	fProviderId = providerId;
	return B_OK;
}


status_t
BAccount::SetLabel(const char* label)
{
	if (label == NULL)
		return B_BAD_VALUE;

	fLabel = label;
	return B_OK;
}


status_t
BAccount::SetUserName(const char* userName)
{
	if (userName == NULL)
		return B_BAD_VALUE;

	fUserName = userName;
	return B_OK;
}


void
BAccount::SetCapabilities(uint32 capabilities)
{
	fCapabilities = capabilities;
}


status_t
BAccount::SetSetting(const char* key, const char* value)
{
	if (key == NULL || value == NULL)
		return B_BAD_VALUE;

	fSettings.RemoveName(key);
	fSettings.AddString(key, value);
	return B_OK;
}


status_t
BAccount::SetSetting(const char* key, int32 value)
{
	if (key == NULL)
		return B_BAD_VALUE;

	fSettings.RemoveName(key);
	fSettings.AddInt32(key, value);
	return B_OK;
}


status_t
BAccount::SetSetting(const char* key, bool value)
{
	if (key == NULL)
		return B_BAD_VALUE;

	fSettings.RemoveName(key);
	fSettings.AddBool(key, value);
	return B_OK;
}


status_t
BAccount::GetSetting(const char* key, BString& value) const
{
	if (key == NULL)
		return B_BAD_VALUE;

	const char* string = NULL;
	if (fSettings.FindString(key, &string) != B_OK)
		return B_NAME_NOT_FOUND;

	value = string;
	return B_OK;
}


status_t
BAccount::GetSetting(const char* key, int32& value) const
{
	if (key == NULL)
		return B_BAD_VALUE;

	return fSettings.FindInt32(key, &value);
}


status_t
BAccount::GetSetting(const char* key, bool& value) const
{
	if (key == NULL)
		return B_BAD_VALUE;

	return fSettings.FindBool(key, &value);
}


status_t
BAccount::RetrievePassword(BString& password) const
{
	if (fId.Length() == 0)
		return B_BAD_VALUE;

	BKeyStore store;
	BPasswordKey key;
	status_t status = store.GetKey(kKeyringName, B_KEY_TYPE_PASSWORD,
		fId.String(), kPasswordSecondaryId, key);
	if (status != B_OK)
		return status;

	password = key.Password();
	return B_OK;
}


status_t
BAccount::StorePassword(const char* password)
{
	if (fId.Length() == 0 || password == NULL)
		return B_BAD_VALUE;

	BKeyStore store;
	BPasswordKey key(password, B_KEY_PURPOSE_NETWORK, fId.String(),
		kPasswordSecondaryId);

	status_t status = store.AddKey(kKeyringName, key);
	if (status == B_OK)
		return B_OK;

	// The keyring appears on first use; add it and retry once.
	status = store.AddKeyring(kKeyringName);
	if (status != B_OK && status != B_FILE_EXISTS)
		return status;

	return store.AddKey(kKeyringName, key);
}


status_t
BAccount::GetTokenAsync(const BMessenger& replyTo)
{
	AccountRequest* request = new AccountRequest(*this, replyTo);
	if (request == NULL)
		return B_NO_MEMORY;

	thread_id thread = spawn_thread(&AccountRequest::ThreadEntry,
		"account token", B_NORMAL_PRIORITY, request);
	if (thread < B_OK) {
		delete request;
		return thread;
	}

	resume_thread(thread);
	return B_OK;
}


BAccount&
BAccount::operator=(const BAccount& other)
{
	fId = other.fId;
	fProviderId = other.fProviderId;
	fLabel = other.fLabel;
	fUserName = other.fUserName;
	fCapabilities = other.fCapabilities;
	fCreationTime = other.fCreationTime;
	fModificationTime = other.fModificationTime;
	fSettings = other.fSettings;
	return *this;
}


bool
BAccount::operator==(const BAccount& other) const
{
	return fId == other.fId;
}


bool
BAccount::operator!=(const BAccount& other) const
{
	return !(*this == other);
}
