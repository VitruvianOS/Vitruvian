/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SHARED_BRW_LOCKER_H
#define _SHARED_BRW_LOCKER_H


#include <OS.h>
#include <SupportDefs.h>


/* Reader/writer futex lock. Writers nest and may also read. Readers do not
 * nest and cannot upgrade, a reader that waits on a writer deadlocks. */
class BRWLocker {
public:
							BRWLocker();
							BRWLocker(const char* name);
	virtual					~BRWLocker();

		status_t			InitCheck() const;

		bool				ReadLock();
		bool				ReadUnlock();
		bool				IsReadLocked() const;

		bool				WriteLock();
		bool				WriteUnlock();
		bool				IsWriteLocked() const;

private:
							BRWLocker(const BRWLocker&);
		BRWLocker&			operator=(const BRWLocker&);
		void				_ReleaseWrite();

		volatile int32		fState;
		thread_id			fWriterThread;
		int32				fWriterNest;
		int32				fWriterReadNest;
		int32				_reserved[4];
};


#if DEBUG
#define ASSERT_BRW_LOCKED(locker) \
	do { \
		if (!((locker).IsWriteLocked() || (locker).IsReadLocked())) \
			debugger("BRWLocker: not locked"); \
	} while (0)

#define ASSERT_BRW_READ_LOCKED(locker) \
	do { \
		if (!(locker).IsReadLocked()) \
			debugger("BRWLocker: not read locked"); \
	} while (0)

#define ASSERT_BRW_WRITE_LOCKED(locker) \
	do { \
		if (!(locker).IsWriteLocked()) \
			debugger("BRWLocker: not write locked"); \
	} while (0)
#else
#define ASSERT_BRW_LOCKED(locker)		do {} while (0)
#define ASSERT_BRW_READ_LOCKED(locker)	do {} while (0)
#define ASSERT_BRW_WRITE_LOCKED(locker)	do {} while (0)
#endif


class AutoWriteLocker {
public:
	AutoWriteLocker(BRWLocker* lock)
		:
		fLock(*lock)
	{
		fLocked = fLock.WriteLock();
	}

	AutoWriteLocker(BRWLocker& lock)
		:
		fLock(lock)
	{
		fLocked = fLock.WriteLock();
	}

	~AutoWriteLocker()
	{
		Unlock();
	}

	bool IsLocked() const
	{
		return fLocked;
	}

	void Unlock()
	{
		if (fLocked) {
			fLock.WriteUnlock();
			fLocked = false;
		}
	}

private:
	BRWLocker&	fLock;
	bool		fLocked;
};


class AutoReadLocker {
public:
	AutoReadLocker(BRWLocker* lock)
		:
		fLock(*lock)
	{
		fLocked = fLock.ReadLock();
	}

	AutoReadLocker(BRWLocker& lock)
		:
		fLock(lock)
	{
		fLocked = fLock.ReadLock();
	}

	~AutoReadLocker()
	{
		Unlock();
	}

	void Unlock()
	{
		if (fLocked) {
			fLock.ReadUnlock();
			fLocked = false;
		}
	}

private:
	BRWLocker&	fLock;
	bool		fLocked;
};


#endif	// _SHARED_BRW_LOCKER_H
