/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */
#ifndef _LOCAL_SEND_SETTINGS_H
#define _LOCAL_SEND_SETTINGS_H

#include <Message.h>
#include <String.h>

#include <vector>

// One completed transfer kept in the history list.
struct LsHistoryEntry {
	BString		peerAlias;
	BString		fileNames;	// comma separated
	int64		bytes;
	bool		outgoing;
	bool		success;
	int64		when;		// unix time

					LsHistoryEntry()
						:
						bytes(0),
						outgoing(true),
						success(false),
						when(0)
					{
					}
};


// App settings persisted as a flattened BMessage in the user's
// settings directory. Also holds the transfer history and the
// always-allow fingerprint list.
class LocalSendSettings {
public:
						LocalSendSettings();

			bool		Load();
			bool		Save() const;

			const BString&	Alias() const { return fAlias; }
			void		SetAlias(const BString& alias) { fAlias = alias; }

			const BString&	DestinationFolder() const
								{ return fDestination; }
			void		SetDestinationFolder(const BString& folder)
								{ fDestination = folder; }

			bool		RequirePin() const { return fRequirePin; }
			void		SetRequirePin(bool require) { fRequirePin = require; }

			const BString&	Pin() const { return fPin; }
			void		SetPin(const BString& pin) { fPin = pin; }

			// Stable across runs so peers remember us.
			const BString&	Fingerprint() const { return fFingerprint; }
			void		SetFingerprint(const BString& fingerprint)
								{ fFingerprint = fingerprint; }

			bool		IsTrusted(const BString& fingerprint) const;
			void		AddTrusted(const BString& fingerprint);

			void		AddHistory(const LsHistoryEntry& entry);
			const std::vector<LsHistoryEntry>&	History() const
								{ return fHistory; }
			void		ClearHistory() { fHistory.clear(); }

private:
			BString		fAlias;
			BString		fDestination;
			BString		fPin;
			BString		fFingerprint;
			bool		fRequirePin;
			std::vector<BString>	fTrusted;
			std::vector<LsHistoryEntry>	fHistory;
};

#endif // _LOCAL_SEND_SETTINGS_H
