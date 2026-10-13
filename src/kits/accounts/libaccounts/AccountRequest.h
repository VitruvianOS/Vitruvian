/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _ACCOUNT_REQUEST_H
#define _ACCOUNT_REQUEST_H


#include <Account.h>
#include <Messenger.h>


// Worker that runs BAccount::GetTokenAsync() off the caller's thread:
// loads the provider add-on, collects the token and posts the reply.
// Freed by ThreadEntry once the reply is out.
class AccountRequest {
public:
								AccountRequest(const BAccount& account,
									const BMessenger& replyTo);

	static	int32				ThreadEntry(void* data);
			void				Run();

private:
			BAccount			fAccount;
			BMessenger			fReplyTo;
};


#endif // _ACCOUNT_REQUEST_H
