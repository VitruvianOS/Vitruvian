/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef VOS_SESSION_ADAPTER_H
#define VOS_SESSION_ADAPTER_H


#include <mutex>
#include <string>
#include <vector>
#include <wpe/webkit.h>


struct WebHistoryEntry {
	int64		timestamp;
	std::string	title;
	std::string	url;
};


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
			void		ClearHistory();
			void		DeleteCookiesForDomain(const std::string& domain);

	// Persists a page visit in the profile history file; ephemeral
	// sessions keep nothing.
			void		RecordVisit(const std::string& url,
							const std::string& title);

private:
			void		_EnsureProfileDir();
			void		_LoadHistory();
			void		_SaveHistory();

			std::string			fProfileDir;
			bool				fEphemeral;
	WebKitNetworkSession*	fNetworkSession;
	std::mutex				fHistoryMutex;
	std::vector<WebHistoryEntry>	fHistory;
};


#endif // VOS_SESSION_ADAPTER_H
