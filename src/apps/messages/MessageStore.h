/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MESSAGE_STORE_H
#define _MESSAGE_STORE_H


#include <ObjectList.h>
#include <String.h>

#include <time.h>


struct StoredSms {
	BString		number;
	BString		text;
	const char*	direction;	// "in" or "out"
	const char*	state;		// "sending", "sent", "failed", "received"
	time_t		timestamp;
};


struct SmsThread {
	BString						number;
	BObjectList<StoredSms>		messages;
};


// SMS threads keyed by phone number, persisted flat in
// ~/config/settings/Messages/store. Newest thread first.
class MessageStore {
public:
								MessageStore();

			int32				CountThreads() const
									{ return fThreads.CountItems(); }
			SmsThread*			ThreadAt(int32 index) const;

			SmsThread*			FindOrAddThread(const char* number);
			SmsThread*			FindThread(const char* number) const;

			void				AddIncoming(const char* number,
									const char* text);
			// Returns the stored entry so the caller can update its
			// state when the delivery report lands.
			StoredSms*			AddOutgoing(const char* number,
									const char* text);
			void				UpdateLastOutgoingState(const char* number,
									const char* state);
			void				RemoveThread(const char* number);

			void				Load();
			void				Save();

private:
			BObjectList<SmsThread>	fThreads;
};


#endif // _MESSAGE_STORE_H
