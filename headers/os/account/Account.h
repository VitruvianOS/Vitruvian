/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _ACCOUNT_H
#define _ACCOUNT_H


#include <Message.h>
#include <String.h>

#include <AccountsDefs.h>


class BMessenger;


class BAccount {
public:
								BAccount();
								BAccount(const BAccount& other);
								BAccount(const BMessage& archive);
								~BAccount();

			status_t			Archive(BMessage* archive) const;

			const char*			Id() const;
			const char*			ProviderId() const;
			const char*			Label() const;
			const char*			UserName() const;
			uint32				Capabilities() const;
			bigtime_t			CreationTime() const;
			bigtime_t			ModificationTime() const;

			status_t			SetId(const char* id);
			status_t			SetProviderId(const char* providerId);
			status_t			SetLabel(const char* label);
			status_t			SetUserName(const char* userName);
			void				SetCapabilities(uint32 capabilities);

			status_t			SetSetting(const char* key, const char* value);
			status_t			SetSetting(const char* key, int32 value);
			status_t			SetSetting(const char* key, bool value);
			status_t			GetSetting(const char* key, BString& value) const;
			status_t			GetSetting(const char* key, int32& value) const;
			status_t			GetSetting(const char* key, bool& value) const;

			// Secrets live in the key store, never in the account file.
			status_t			RetrievePassword(BString& password) const;
			status_t			StorePassword(const char* password);

			// Asks the provider add-on for an access token, transparently
			// refreshing it when needed. The reply lands on replyTo with
			// what kAccountTokenResult; the provider may also post
			// kAccountConsentRequest carrying the consent page URL.
			status_t			GetTokenAsync(const BMessenger& replyTo);

			BAccount&			operator=(const BAccount& other);

			bool				operator==(const BAccount& other) const;
			bool				operator!=(const BAccount& other) const;

private:
			BString				fId;
			BString				fProviderId;
			BString				fLabel;
			BString				fUserName;
			uint32				fCapabilities;
			bigtime_t			fCreationTime;
			bigtime_t			fModificationTime;
			BMessage			fSettings;
};


#endif // _ACCOUNT_H
