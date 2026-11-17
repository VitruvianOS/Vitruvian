/*
 * Copyright 2007, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 */
#ifndef PROFILE_MESSAGE_SUPPORT_H
#define PROFILE_MESSAGE_SUPPORT_H


#include <OS.h>
#include <String.h>


const char* string_for_message_code(uint32 code);


// Counts the codes that block the client on a reply (VOS_COMPOSE_HOLD_STATS),
// and prints the top ones once per second.
class ReplyStats {
public:
								ReplyStats();

			bool				Enabled() const { return fEnabled; }
			void				Count(int32 code);
			void				Report(const char* kind, const char* name);

private:
			enum { kSlots = 64 };

			bool				fEnabled;
			bigtime_t			fSince;
			int32				fTotal;
			int32				fCodes[kSlots];
			int32				fCounts[kSlots];
			int32				fUsed;
};


#endif // PROFILE_MESSAGE_SUPPORT_H
