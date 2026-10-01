/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef VOS_SESSION_ADAPTER_H
#define VOS_SESSION_ADAPTER_H


#include <string>
#include <wpe/webkit.h>


class SessionAdapter {
public:
	explicit			SessionAdapter(const std::string& profileDir,
							bool ephemeral);
						~SessionAdapter();

	WebKitNetworkSession*	NetworkSession() const { return fNetworkSession; }
	bool					IsEphemeral() const { return fEphemeral; }
	const std::string&		ProfileDir() const { return fProfileDir; }

			void		ClearCookies();
			void		ClearCache();
			void		DeleteCookiesForDomain(const std::string& domain);

private:
			void		_EnsureProfileDir();

			std::string			fProfileDir;
			bool				fEphemeral;
	WebKitNetworkSession*	fNetworkSession;
};


#endif // VOS_SESSION_ADAPTER_H
