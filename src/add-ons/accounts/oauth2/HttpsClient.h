/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _HTTPS_CLIENT_H
#define _HTTPS_CLIENT_H


#include <String.h>


// Minimal HTTPS/1.1 client, enough for OAuth2 token endpoints. Uses
// OpenSSL directly; nothing of it leaks into the public accounts API.
status_t HttpsPost(const char* url, const BString& body, BString& response);

status_t UrlEncode(const BString& value, BString& encoded);


#endif // _HTTPS_CLIENT_H
