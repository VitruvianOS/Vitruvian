/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <OS.h>
#include <SupportDefs.h>

#include <BRWLocker.h>
#include <libroot/futex.h>


#define kWriter			((int32)1 << 31)
#define kWriterWaiting	((int32)1 << 30)
#define kReaderMask		(~(kWriter | kWriterWaiting))


BRWLocker::BRWLocker()
	:
	fState(0),
	fWriterThread(B_ERROR),
	fWriterNest(0),
	fWriterReadNest(0)
{
	_reserved[0] = 0;
	_reserved[1] = 0;
	_reserved[2] = 0;
	_reserved[3] = 0;
}


BRWLocker::BRWLocker(const char*)
	:
	fState(0),
	fWriterThread(B_ERROR),
	fWriterNest(0),
	fWriterReadNest(0)
{
	_reserved[0] = 0;
	_reserved[1] = 0;
	_reserved[2] = 0;
	_reserved[3] = 0;
}


BRWLocker::~BRWLocker()
{
}


status_t
BRWLocker::InitCheck() const
{
	return B_OK;
}


bool
BRWLocker::ReadLock()
{
	thread_id self = find_thread(NULL);

	// The writer may also read, nested.
	if (fWriterThread == self) {
		fWriterReadNest++;
		return true;
	}

	for (;;) {
		int32 current = __atomic_load_n(&fState, __ATOMIC_ACQUIRE);
		// Readers give way to a waiting writer.
		if ((current & (kWriter | kWriterWaiting)) == 0) {
			if (__atomic_compare_exchange_n(&fState, &current, current + 1,
					false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
				return true;
			}
			continue;
		}
		futex_wait(&fState, current, 0);
	}
}


bool
BRWLocker::ReadUnlock()
{
	thread_id self = find_thread(NULL);
	if (fWriterThread == self) {
		if (fWriterReadNest > 0
			&& --fWriterReadNest == 0 && fWriterNest == 0)
			_ReleaseWrite();
		return true;
	}

	int32 current = __atomic_load_n(&fState, __ATOMIC_ACQUIRE);
	do {
		if ((current & kReaderMask) == 0) {
			debugger("BRWLocker: no readers");
			return false;
		}
	} while (!__atomic_compare_exchange_n(&fState, &current, current - 1,
			false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));

	int32 old = current - 1;
	if ((old & kReaderMask) == 0 && (old & (kWriter | kWriterWaiting)) != 0)
		futex_wake_all(&fState);
	return true;
}


bool
BRWLocker::IsReadLocked() const
{
	int32 state = __atomic_load_n(&fState, __ATOMIC_ACQUIRE);
	if ((state & kReaderMask) != 0)
		return true;
	return fWriterThread == find_thread(NULL) && fWriterReadNest > 0;
}


bool
BRWLocker::WriteLock()
{
	thread_id self = find_thread(NULL);
	if (fWriterThread == self) {
		fWriterNest++;
		return true;
	}

	__atomic_fetch_or(&fState, kWriterWaiting, __ATOMIC_ACQ_REL);
	for (;;) {
		int32 current = __atomic_load_n(&fState, __ATOMIC_ACQUIRE);
		if ((current & (kWriter | kReaderMask)) == 0) {
			int32 want = kWriter;
			if (__atomic_compare_exchange_n(&fState, &current, want, false,
					__ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
				fWriterThread = self;
				fWriterNest = 1;
				fWriterReadNest = 0;
				return true;
			}
			continue;
		}
		// The winner cleared the bit, so set it again and wait on the
		// value that has it.
		__atomic_fetch_or(&fState, kWriterWaiting, __ATOMIC_ACQ_REL);
		futex_wait(&fState, current | kWriterWaiting, 0);
	}
}


bool
BRWLocker::WriteUnlock()
{
	thread_id self = find_thread(NULL);
	if (fWriterThread != self) {
		debugger("BRWLocker: unlocking a write lock it does not hold");
		return false;
	}

	// Reads taken as the writer keep the lock until they are released.
	if (--fWriterNest > 0 || fWriterReadNest > 0)
		return true;

	_ReleaseWrite();
	return true;
}


void
BRWLocker::_ReleaseWrite()
{
	fWriterThread = B_ERROR;
	__atomic_fetch_and(&fState, ~kWriter, __ATOMIC_RELEASE);
	futex_wake_all(&fState);
}


bool
BRWLocker::IsWriteLocked() const
{
	return fWriterThread == find_thread(NULL) && fWriterNest > 0;
}
