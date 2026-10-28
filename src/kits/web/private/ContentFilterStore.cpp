/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "ContentFilterStore.h"

#include "logging.h"
#include "process_manager.h"

#include <cstdio>


// Carried through the async store calls; deleted in the finish
// handler.
struct FilterTask {
	ContentFilterStore*	self;
	std::string			id;
	std::string			jsonPath;
	int64				startUs;
};


ContentFilterStore&
ContentFilterStore::Instance()
{
	static ContentFilterStore store;
	return store;
}


ContentFilterStore::ContentFilterStore()
	:
	fStore(NULL),
	fCacheDirSet(false)
{
}


ContentFilterStore::~ContentFilterStore()
{
	for (auto& entry : fCompiled) {
		if (entry.second != NULL)
			webkit_user_content_filter_unref(entry.second);
	}
	if (fStore != NULL)
		g_object_unref(fStore);
}


void
ContentFilterStore::SetCacheDir(const std::string& dir)
{
	std::lock_guard<std::mutex> lock(fMutex);
	if (fCacheDirSet)
		return;
	fCacheDir = dir;
	fCacheDirSet = true;
}


void
ContentFilterStore::Get(const std::string& id, const std::string& jsonPath,
	std::function<void(WebKitUserContentFilter*)> cb)
{
	ProcessManager::Instance().InvokeOnGLibThreadSync([&]() {
		_GetOnGLibThread(id, jsonPath, cb);
	});
}


void
ContentFilterStore::_GetOnGLibThread(const std::string& id,
	const std::string& jsonPath,
	std::function<void(WebKitUserContentFilter*)> cb)
{
	WebKitUserContentFilter* compiled = NULL;
	{
		std::lock_guard<std::mutex> lock(fMutex);
		auto it = fCompiled.find(id);
		if (it != fCompiled.end())
			compiled = it->second;
		if (fCacheDir.empty()) {
			// No profile yet: nothing to compile into.
			if (cb != NULL)
				cb(NULL);
			return;
		}
		if (fStore == NULL)
			fStore = webkit_user_content_filter_store_new(
				fCacheDir.c_str());
		if (compiled == NULL) {
			Pending* pending = NULL;
			auto pendingIt = fPending.find(id);
			if (pendingIt != fPending.end())
				pending = pendingIt->second;
			else {
				pending = new Pending;
				fPending[id] = pending;
			}
			if (cb != NULL)
				pending->callbacks.push_back(cb);
			if (pending->callbacks.size() > 1)
				return;	// a request is already in flight
		}
	}
	if (compiled != NULL) {
		if (cb != NULL)
			cb(compiled);
		return;
	}

	// First try the on-disk cache; a miss compiles from the JSON.
	FilterTask* task = new FilterTask{this, id, jsonPath,
		g_get_monotonic_time()};
	webkit_user_content_filter_store_load(fStore, id.c_str(), NULL,
		_OnLoadDone, task);
}


void
ContentFilterStore::_Compile(const std::string& id,
	const std::string& jsonPath)
{
	GFile* file = g_file_new_for_path(jsonPath.c_str());
	FilterTask* task = new FilterTask{this, id, jsonPath,
		g_get_monotonic_time()};
	webkit_user_content_filter_store_save_from_file(fStore, id.c_str(),
		file, NULL, _OnSaveDone, task);
	g_object_unref(file);
}


void
ContentFilterStore::_Finish(const std::string& id,
	WebKitUserContentFilter* filter, const char* what, GError* error)
{
	Pending* pending = NULL;
	{
		std::lock_guard<std::mutex> lock(fMutex);
		auto it = fPending.find(id);
		if (it != fPending.end()) {
			pending = it->second;
			fPending.erase(it);
		}
		if (filter != NULL) {
			auto old = fCompiled.find(id);
			if (old != fCompiled.end()
				&& old->second != NULL)
				webkit_user_content_filter_unref(old->second);
			fCompiled[id] = filter;
		}
	}
	if (filter != NULL)
		log_info("NetPlus: content filter %s %s", id.c_str(), what);
	else
		log_error("NetPlus: content filter %s failed: %s",
			id.c_str(), error != NULL ? error->message : "unknown");
	if (pending != NULL) {
		for (auto& callback : pending->callbacks) {
			if (callback != NULL)
				callback(filter);
		}
		delete pending;
	}
}


/* static */
void
ContentFilterStore::_OnLoadDone(GObject* source, GAsyncResult* result,
	gpointer data)
{
	FilterTask* task = static_cast<FilterTask*>(data);
	GError* error = NULL;
	WebKitUserContentFilter* filter
		= webkit_user_content_filter_store_load_finish(
			WEBKIT_USER_CONTENT_FILTER_STORE(source), result, &error);
	if (filter != NULL) {
		int64 ms = (g_get_monotonic_time() - task->startUs) / 1000;
		char what[64];
		snprintf(what, sizeof(what), "loaded in %lld ms",
			(long long)ms);
		task->self->_Finish(task->id, filter, what, NULL);
		g_clear_error(&error);
		delete task;
		return;
	}
	g_clear_error(&error);
	// Nothing cached yet: compile the JSON once.
	task->startUs = g_get_monotonic_time();
	task->self->_Compile(task->id, task->jsonPath);
}


/* static */
void
ContentFilterStore::_OnSaveDone(GObject* source, GAsyncResult* result,
	gpointer data)
{
	FilterTask* task = static_cast<FilterTask*>(data);
	GError* error = NULL;
	WebKitUserContentFilter* filter
		= webkit_user_content_filter_store_save_from_file_finish(
			WEBKIT_USER_CONTENT_FILTER_STORE(source), result, &error);
	if (filter != NULL) {
		int64 ms = (g_get_monotonic_time() - task->startUs) / 1000;
		char what[64];
		snprintf(what, sizeof(what), "compiled in %lld ms",
			(long long)ms);
		task->self->_Finish(task->id, filter, what, NULL);
		g_clear_error(&error);
		delete task;
		return;
	}
	task->self->_Finish(task->id, NULL, NULL, error);
	g_clear_error(&error);
	delete task;
}
