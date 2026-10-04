/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <OS.h>
#include <SupportDefs.h>

#include <atomic>
#include <map>
#include <thread>

namespace {

std::atomic<int32> sNextThreadId(1);

struct ThreadEntry {
	thread_func func;
	void* data;
	std::thread thread;
	status_t result;
};

std::map<int32, ThreadEntry*>&
ThreadTable()
{
	static std::map<int32, ThreadEntry*>* table
		= new std::map<int32, ThreadEntry*>();
	return *table;
}


int32
_RunThread(void* cookie)
{
	ThreadEntry* entry = (ThreadEntry*)cookie;
	entry->result = entry->func(entry->data);
	return 0;
}

} // namespace


thread_id
spawn_thread(thread_func func, const char* name, int32 priority, void* data)
{
	(void)name;
	(void)priority;
	if (func == NULL)
		return B_BAD_VALUE;

	ThreadEntry* entry = new ThreadEntry;
	entry->func = func;
	entry->data = data;
	entry->result = B_OK;

	int32 id = sNextThreadId.fetch_add(1);
	ThreadTable()[id] = entry;
	entry->thread = std::thread(_RunThread, entry);
	return id;
}


status_t
resume_thread(thread_id thread)
{
	(void)thread;
	return B_OK;
}


status_t
wait_for_thread(thread_id thread, status_t* returnValue)
{
	std::map<int32, ThreadEntry*>::iterator it = ThreadTable().find(thread);
	if (it == ThreadTable().end())
		return B_BAD_VALUE;

	ThreadEntry* entry = it->second;
	if (entry->thread.joinable())
		entry->thread.join();
	if (returnValue != NULL)
		*returnValue = entry->result;
	delete entry;
	ThreadTable().erase(it);
	return B_OK;
}
