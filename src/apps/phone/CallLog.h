/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _CALL_LOG_H
#define _CALL_LOG_H


#include <String.h>
#include <ObjectList.h>

#include <time.h>


struct CallLogEntry {
	BString		number;
	const char*	direction;	// "in" or "out"
	const char*	result;		// "completed", "missed", "rejected"
	time_t		timestamp;
};


// Flat call history in ~/config/settings/Phone/calllog; newest first.
class CallLog {
public:
								CallLog();

			int32				CountEntries() const
									{ return fEntries.CountItems(); }
			const CallLogEntry*	EntryAt(int32 index) const;

			void				Add(const CallLogEntry& entry);
			void				Load();
			void				Save();

private:
			BObjectList<CallLogEntry>	fEntries;
};


#endif // _CALL_LOG_H
