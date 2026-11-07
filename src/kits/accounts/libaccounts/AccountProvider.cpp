/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <AccountProvider.h>


BAccountProvider::~BAccountProvider()
{
}


status_t
BAccountProvider::EditAccount(BAccount& /*account*/,
	const BMessenger& /*replyTo*/)
{
	return B_UNSUPPORTED;
}


status_t
BAccountProvider::Authenticate(BAccount& /*account*/,
	const BMessage& /*parameters*/)
{
	return B_OK;
}


status_t
BAccountProvider::GetToken(BAccount& /*account*/,
	const BMessenger& /*consentTo*/, BMessage& /*token*/)
{
	return B_UNSUPPORTED;
}


status_t
BAccountProvider::RemoveAccount(BAccount& /*account*/)
{
	return B_OK;
}
