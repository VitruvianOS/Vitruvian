/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _OS_SUPPORT_SHARED_RW_LOCKER_H
#define _OS_SUPPORT_SHARED_RW_LOCKER_H


#include <OS.h>
#include <SupportDefs.h>


/* Reader/writer lock on a shared word, placed as for BSharedLocker.
 * Writers go first. Bit 31 is the writer, bit 30 a waiting writer, bit 29
 * the dead bit and bits 0 to 28 count readers.
 *
 * The survivor that learns a peer died holding the lock calls MarkDead():
 * every lock call then fails with B_BAD_SEM_ID, unlocks do nothing and
 * blocked waiters are woken. */
struct shared_rw_lock_state {
	int32	fState;
	int32	_reserved[3];
};


class BSharedRWLocker {
public:
							BSharedRWLocker();
							BSharedRWLocker(
								shared_rw_lock_state* state);
	virtual					~BSharedRWLocker();

			void				SetTo(shared_rw_lock_state* state);

			bool				ReadLock();
			status_t			ReadLockWithTimeout(bigtime_t timeout);
			void				ReadUnlock();

			bool				WriteLock();
			status_t			WriteLockWithTimeout(bigtime_t timeout);
			void				WriteUnlock();

			bool				IsReadLocked() const;
			bool				IsWriteLocked() const;

			void				MarkDead();
			bool				IsDead() const;

private:
							BSharedRWLocker(const BSharedRWLocker&);
			BSharedRWLocker&	operator=(const BSharedRWLocker&);

			shared_rw_lock_state*	fState;
};


#endif	// _OS_SUPPORT_SHARED_RW_LOCKER_H
