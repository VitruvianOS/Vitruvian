/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _FRAME_RING_H
#define _FRAME_RING_H


#include <atomic>
#include <mutex>

#include <SupportDefs.h>


namespace BPrivate {


/*!	Slot bookkeeping for a ring of frame buffers shared by a producer and a
	consumer. The producer never waits on drawing: it takes a free slot or
	steals the oldest presented one. Thread safe, reference counted so the
	consumer can outlive the producer that created it.
*/
class FrameRing {
public:
	enum {
		kFree		= 0,
		kDecoding	= 1,
		kPresented	= 2,
		kHeld		= 3,
	};

						FrameRing();

			// A new ring holds one reference. The last Release() deletes it.
			void		AddRef();
			void		Release();

			status_t	SetSlotCount(int32 count);
			int32		SlotCount() const { return fSlotCount; }
			void		Reset();

			// Decoder side.
			int32		AcquireDecoderSlot();
			status_t	PublishSlot(int32 slot, int64* _sequence = NULL);

			// Consumer side. ClaimSlot holds a presented slot until
			// ReleaseSlot; SlotUnchanged says whether the borrowed pixels
			// still hold the frame.
			bool		ClaimSlot(int32 slot, int64 sequence);
			void		ReleaseSlot(int32 slot);
			bool		SlotUnchanged(int32 slot, int64 sequence) const;

			// Bumped by Reset, so a consumer can reject stale frames.
			int64		Generation() const;

private:
						~FrameRing();

	static const int32	kMaxSlots = 4;

			int32		_RoundRobinFreeSlot();
			void		_ResetLocked();

			mutable std::mutex fLock;
			std::atomic<int32> fRefs;

			int32		fSlotCount;
			int32		fState[kMaxSlots];
			int64		fSequence[kMaxSlots];
			int64		fNextSequence;
			int64		fGeneration;
			int32		fNextAcquire;
			int32		fStolen;
			int32		fDropped;
			int32		fSkipped;
};

inline
FrameRing::FrameRing()
	:
	fRefs(1),
	fSlotCount(0),
	fNextSequence(0),
	fGeneration(0),
	fNextAcquire(0),
	fStolen(0),
	fDropped(0),
	fSkipped(0)
{
	_ResetLocked();
}


inline
FrameRing::~FrameRing()
{
}


inline void
FrameRing::AddRef()
{
	fRefs.fetch_add(1);
}


inline void
FrameRing::Release()
{
	if (fRefs.fetch_sub(1) == 1)
		delete this;
}


inline int64
FrameRing::Generation() const
{
	std::lock_guard<std::mutex> _(fLock);
	return fGeneration;
}


inline status_t
FrameRing::SetSlotCount(int32 count)
{
	if (count < 2 || count > kMaxSlots)
		return B_BAD_VALUE;

	std::lock_guard<std::mutex> _(fLock);
	fSlotCount = count;
	_ResetLocked();
	return B_OK;
}


inline void
FrameRing::Reset()
{
	std::lock_guard<std::mutex> _(fLock);
	_ResetLocked();
}


inline void
FrameRing::_ResetLocked()
{
	for (int32 i = 0; i < kMaxSlots; i++) {
		fState[i] = kFree;
		fSequence[i] = -1;
	}
	fNextSequence = 0;
	fNextAcquire = 0;
	fGeneration++;
	fStolen = 0;
	fDropped = 0;
	fSkipped = 0;
}


inline int32
FrameRing::_RoundRobinFreeSlot()
{
	for (int32 i = 0; i < fSlotCount; i++) {
		const int32 slot = (fNextAcquire + i) % fSlotCount;
		if (fState[slot] == kFree) {
			fNextAcquire = (slot + 1) % fSlotCount;
			return slot;
		}
	}
	return -1;
}


inline int32
FrameRing::AcquireDecoderSlot()
{
	std::lock_guard<std::mutex> _(fLock);
	const int32 freeSlot = _RoundRobinFreeSlot();
	if (freeSlot >= 0) {
		fState[freeSlot] = kDecoding;
		fSequence[freeSlot] = -1;
		return freeSlot;
	}

	// Steal the oldest presented slot, held ones are still being drawn.
	int32 oldest = -1;
	int64 oldestSequence = 0;
	for (int32 i = 0; i < fSlotCount; i++) {
		if (fState[i] != kPresented)
			continue;
		if (oldest < 0 || fSequence[i] < oldestSequence) {
			oldest = i;
			oldestSequence = fSequence[i];
		}
	}

	if (oldest < 0) {
		fDropped++;
		return -1;
	}

	fStolen++;
	fState[oldest] = kDecoding;
	fSequence[oldest] = -1;
	fNextAcquire = (oldest + 1) % fSlotCount;
	return oldest;
}


inline status_t
FrameRing::PublishSlot(int32 slot, int64* _sequence)
{
	std::lock_guard<std::mutex> _(fLock);
	if (slot < 0 || slot >= fSlotCount || fState[slot] != kDecoding)
		return B_BAD_VALUE;

	fState[slot] = kPresented;
	fSequence[slot] = fNextSequence++;
	if (_sequence != NULL)
		*_sequence = fSequence[slot];

	return B_OK;
}


inline bool
FrameRing::ClaimSlot(int32 slot, int64 sequence)
{
	std::lock_guard<std::mutex> _(fLock);
	if (slot < 0 || slot >= fSlotCount
			|| fState[slot] != kPresented
			|| fSequence[slot] != sequence) {
		fSkipped++;
		return false;
	}

	fState[slot] = kHeld;
	return true;
}


inline void
FrameRing::ReleaseSlot(int32 slot)
{
	std::lock_guard<std::mutex> _(fLock);
	if (slot < 0 || slot >= fSlotCount)
		return;

	fState[slot] = kFree;
	fSequence[slot] = -1;
}


inline bool
FrameRing::SlotUnchanged(int32 slot, int64 sequence) const
{
	std::lock_guard<std::mutex> _(fLock);
	if (slot < 0 || slot >= fSlotCount)
		return false;

	if (fState[slot] == kFree)
		return true;

	if (fState[slot] == kDecoding)
		return false;

	return fSequence[slot] == sequence;
}


}	// namespace BPrivate


#endif // _FRAME_RING_H
