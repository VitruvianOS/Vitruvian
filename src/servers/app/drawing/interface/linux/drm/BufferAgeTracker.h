/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _BUFFER_AGE_TRACKER_H
#define _BUFFER_AGE_TRACKER_H


#include <Region.h>


/*!	Per-scanout-buffer age bookkeeping. Slots are caller-defined indexes
	of the framebuffers. Not thread safe, the caller serializes access.
*/
class BufferAgeTracker {
public:
						BufferAgeTracker();

			void		Init(int32 bufferCount);
			void		Reset();

			void		AddDamage(const BRegion& region);
			void		AddDamage(const BRect& rect);
			int32		PendingDamageCount() const;

			// Folds the pending damage into the history under serial, marks
			// presentedSlot as scanned out at it and fills _stale with what
			// writeSlot is missing.
			void		CommitFrame(int64 serial, int32 presentedSlot,
								int32 writeSlot, BRegion* _stale);

			// Frames since the slot was scanned out, 1 when it holds the
			// last presented frame, -1 when never scanned.
			int64		AgeOf(int32 slot) const;

			int32		BufferCount() const { return fBufferCount; }
			int64		LatestSerial() const { return fLatestSerial; }

private:
	static const int32	kMaxBuffers = 3;

			void		_ShiftHistory();

			int32		fBufferCount;
			int64		fLatestSerial;
			int64		fLastPresented[kMaxBuffers];
			BRegion		fCurrentDamage;
			// fHistory[0] is the damage of the latest committed frame.
			BRegion		fHistory[kMaxBuffers];
			int32		fHistoryDepth;
};


#endif // _BUFFER_AGE_TRACKER_H
