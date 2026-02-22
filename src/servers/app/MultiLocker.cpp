/*
 * Copyright 2005-2009, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT license.
 *
 * Copyright 1999, Be Incorporated.   All Rights Reserved.
 * This file may be used under the terms of the Be Sample Code License.
 */


#include "MultiLocker.h"

//#include <Debug.h>
#include <Errors.h>
#include <OS.h>

#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <unordered_map>

#define TIMING	MULTI_LOCKER_TIMING


const int32 LARGE_NUMBER = 100000;


MultiLocker::MultiLocker(const char* baseName)
	:
	fReaderCount(0),
	fWriterCount(0),
	fWriterThread(-1),
	fWriterNest(0),
	fInit(B_OK)
{

	fLockMutex = new std::mutex();
	fReadCondition = new std::condition_variable();
	fWriteCondition = new std::condition_variable();


#if TIMING
	//initialize the counter variables
	rl_count = ru_count = wl_count = wu_count = islock_count = 0;
	rl_time = ru_time = wl_time = wu_time = islock_time = 0;
#endif
}


MultiLocker::~MultiLocker()
{
	// become the writer
	if (!IsWriteLocked())
		WriteLock();

	// set locker to be uninitialized
	fInit = B_NO_INIT;

	delete fWriteCondition;
	delete fReadCondition;
	delete fLockMutex;
#if TIMING
	// let's produce some performance numbers
	printf("MultiLocker Statistics:\n"
		"Avg ReadLock: %lld\n"
		"Avg ReadUnlock: %lld\n"
		"Avg WriteLock: %lld\n"
		"Avg WriteUnlock: %lld\n"
		"Avg IsWriteLocked: %lld\n",
		rl_count > 0 ? rl_time / rl_count : 0,
		ru_count > 0 ? ru_time / ru_count : 0,
		wl_count > 0 ? wl_time / wl_count : 0,
		wu_count > 0 ? wu_time / wu_count : 0,
		islock_count > 0 ? islock_time / islock_count : 0);
#endif
}


status_t
MultiLocker::InitCheck()
{
	return fInit;
}



bool
MultiLocker::IsWriteLocked() const
{
#if TIMING
	bigtime_t start = system_time();
#endif

	bool writeLockHolder = false;

	if (fInit == B_OK) {
		thread_id current = find_thread(NULL);
		writeLockHolder = (current == fWriterThread && fWriterCount > 0);
	}

#if TIMING
	bigtime_t end = system_time();
	islock_time += (end - start);
	islock_count++;
#endif

	return writeLockHolder;
}


bool
MultiLocker::ReadLock()
{
#if TIMING
	bigtime_t start = system_time();
#endif

	bool locked = false;

	if (fInit != B_OK)
		return false;

	std::unique_lock<std::mutex> lock(*fLockMutex);
	
	thread_id current = find_thread(NULL);
	
	if (fWriterThread == current && fWriterCount > 0) {
		fWriterNest++;
		locked = true;
	} else {
		while (fWriterCount > 0 && fWriterThread != current) {
			fReadCondition->wait(lock);
		}
		
		fReaderCount++;
		locked = true;
	}

#if TIMING
	bigtime_t end = system_time();
	rl_time += (end - start);
	rl_count++;
#endif

	return locked;
}


bool
MultiLocker::WriteLock()
{
#if TIMING
	bigtime_t start = system_time();
#endif

	bool locked = false;

	if (fInit != B_OK)
		return false;

	std::unique_lock<std::mutex> lock(*fLockMutex);
	
	thread_id current = find_thread(NULL);
	
	if (fWriterThread == current) {
		fWriterNest++;
		locked = true;
	} else {
		while (fReaderCount > 0 || fWriterCount > 0) {
			fWriteCondition->wait(lock);
		}
		
		fWriterCount = 1;
		fWriterThread = current;
		fWriterNest = 1;
		locked = true;
	}

#if TIMING
	bigtime_t end = system_time();
	wl_time += (end - start);
	wl_count++;
#endif

	return locked;
}


bool
MultiLocker::ReadUnlock()
{
#if TIMING
	bigtime_t start = system_time();
#endif

	bool unlocked = false;

	if (fInit != B_OK)
		return false;

	std::unique_lock<std::mutex> lock(*fLockMutex);
	
	thread_id current = find_thread(NULL);
	
	if (fWriterThread == current && fWriterCount > 0) {
		fWriterNest--;
		if (fWriterNest < 0) {
			debugger("ReadUnlock() - negative writer nesting level");
			fWriterNest = 0;
		}
		unlocked = true;
	} else {
		if (fReaderCount <= 0) {
			debugger("ReadUnlock() - no readers");
			return false;
		}
		
		fReaderCount--;
		unlocked = true;
		
		if (fReaderCount == 0) {
			fWriteCondition->notify_one();
		}
	}

#if TIMING
	bigtime_t end = system_time();
	ru_time += (end - start);
	ru_count++;
#endif

	return unlocked;
}


bool
MultiLocker::WriteUnlock()
{
#if TIMING
	bigtime_t start = system_time();
#endif

	bool unlocked = false;

	if (fInit != B_OK)
		return false;

	std::unique_lock<std::mutex> lock(*fLockMutex);
	
	thread_id current = find_thread(NULL);

	if (fWriterThread != current) {
		debugger("WriteUnlock() - not a writer");
		return false;
	}
	
	if (fWriterNest > 1) {
		fWriterNest--;
		unlocked = true;
	} else {
		fWriterCount = 0;
		fWriterThread = -1;
		fWriterNest = 0;
		
		fReadCondition->notify_all();
		fWriteCondition->notify_one();
		unlocked = true;
	}

#if TIMING
	bigtime_t end = system_time();
	wu_time += (end - start);
	wu_count++;
#endif

	return unlocked;
}
