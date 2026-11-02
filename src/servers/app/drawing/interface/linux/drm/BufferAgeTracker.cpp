/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "BufferAgeTracker.h"

#include <string.h>


BufferAgeTracker::BufferAgeTracker()
	:
	fBufferCount(0),
	fLatestSerial(-1),
	fHistoryDepth(0)
{
	memset(fLastPresented, 0xff, sizeof(fLastPresented));
}


void
BufferAgeTracker::Init(int32 bufferCount)
{
	if (bufferCount < 2)
		bufferCount = 2;
	if (bufferCount > kMaxBuffers)
		bufferCount = kMaxBuffers;

	fBufferCount = bufferCount;
	fHistoryDepth = bufferCount;
	Reset();
}


void
BufferAgeTracker::Reset()
{
	for (int32 i = 0; i < kMaxBuffers; i++) {
		fLastPresented[i] = -1;
		fHistory[i].MakeEmpty();
	}
	fLatestSerial = -1;
	fCurrentDamage.MakeEmpty();
}


void
BufferAgeTracker::AddDamage(const BRegion& region)
{
	fCurrentDamage.Include(&region);
}


void
BufferAgeTracker::AddDamage(const BRect& rect)
{
	fCurrentDamage.Include(rect);
}


int32
BufferAgeTracker::PendingDamageCount() const
{
	return fCurrentDamage.CountRects();
}


void
BufferAgeTracker::_ShiftHistory()
{
	for (int32 i = fHistoryDepth - 1; i > 0; i--)
		fHistory[i] = fHistory[i - 1];
	fHistory[0] = fCurrentDamage;
	fCurrentDamage.MakeEmpty();
}


void
BufferAgeTracker::CommitFrame(int64 serial, int32 presentedSlot,
	int32 writeSlot, BRegion* _stale)
{
	if (_stale != NULL)
		_stale->MakeEmpty();

	if (serial > fLatestSerial) {
		_ShiftHistory();
		fLatestSerial = serial;
	}

	if (presentedSlot >= 0 && presentedSlot < fBufferCount)
		fLastPresented[presentedSlot] = serial;

	if (writeSlot < 0 || writeSlot >= fBufferCount || _stale == NULL)
		return;

	const int64 presented = fLastPresented[writeSlot];
	if (presented < 0 || presented > fLatestSerial) {
		// Never scanned out, or reset since: report all known damage.
		_stale->Include(&fCurrentDamage);
		for (int32 i = 0; i < fHistoryDepth; i++)
			_stale->Include(&fHistory[i]);
		return;
	}

	// Age 1 means the slot holds the last presented frame exactly.
	const int64 framesMissed = fLatestSerial - presented;
	for (int64 i = 0; i < framesMissed && i < fHistoryDepth; i++)
		_stale->Include(&fHistory[(int32)i]);
}


int64
BufferAgeTracker::AgeOf(int32 slot) const
{
	if (slot < 0 || slot >= fBufferCount || fLastPresented[slot] < 0)
		return -1;

	return fLatestSerial - fLastPresented[slot] + 1;
}
