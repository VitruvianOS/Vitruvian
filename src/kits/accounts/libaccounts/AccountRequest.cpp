/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "AccountRequest.h"

#include <stdio.h>
#include <string.h>

#include <AccountProvider.h>
#include <AccountRoster.h>
#include <AccountsDefs.h>


AccountRequest::AccountRequest(const BAccount& account,
	const BMessenger& replyTo)
	:
	fAccount(account),
	fReplyTo(replyTo)
{
}


int32
AccountRequest::ThreadEntry(void* data)
{
	AccountRequest* request = static_cast<AccountRequest*>(data);
	request->Run();
	delete request;
	return 0;
}


void
AccountRequest::Run()
{
	BMessage reply(kAccountTokenResult);

	BAccountRoster* roster = BAccountRoster::Default();
	BAccountProvider* provider = NULL;
	status_t status = B_NO_INIT;
	if (roster != NULL)
		status = roster->GetProvider(provider, fAccount.ProviderId());

	if (status == B_OK) {
		BMessage token;
		status = provider->GetToken(fAccount, fReplyTo, token);
		if (status == B_OK) {
			const char* value = NULL;
			if (token.FindString(kAccountsTokenField, &value) == B_OK)
				reply.AddString(kAccountsTokenField, value);

			int64 expiry = 0;
			if (token.FindInt64(kAccountsExpiryField, &expiry) == B_OK)
				reply.AddInt64(kAccountsExpiryField, expiry);
		}
	}

	reply.AddInt32(kAccountsStatusField, status);
	if (status != B_OK)
		reply.AddString(kAccountsReasonField, strerror(status));

	fReplyTo.SendMessage(&reply);
}
