/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "process_manager.h"

#include "logging.h"

#include <cstring>
#include <glib.h>
#include <pthread.h>


static void*
glib_thread_func(void* data)
{
	ProcessManager* pm = static_cast<ProcessManager*>(data);
	log_info("ProcessManager: GLib loop starting");

	// Acquire the context and push it as thread-default BEFORE entering the
	// main loop.  This ensures any g_main_context_invoke() call from another
	// thread will queue onto this context rather than being dispatched
	// directly on the caller's thread (the P1 race).
	g_main_context_push_thread_default(pm->Context());

	// Signal StartGLibLoop() that the context is owned and ready.
	pm->SignalContextReady();

	g_main_loop_run(pm->MainLoop());

	g_main_context_pop_thread_default(pm->Context());

	log_info("ProcessManager: GLib loop exited");
	return nullptr;
}


//	#pragma mark - Constructor/Destructor


ProcessManager::ProcessManager()
	:
	fLoop(nullptr),
	fContext(nullptr),
	fThread(0),
	fRunning(false),
	fContextReady(false)
{
}


ProcessManager::~ProcessManager()
{
	StopGLibLoop();
}


//	#pragma mark - Public


ProcessManager&
ProcessManager::Instance()
{
	static ProcessManager pm;
	return pm;
}


void
ProcessManager::StartGLibLoop()
{
	if (fRunning)
		return;

	fContext = g_main_context_default();
	fLoop = g_main_loop_new(fContext, FALSE);

	fContextReady.store(false, std::memory_order_release);
	pthread_create(&fThread, nullptr, glib_thread_func, this);

	// Block until the GLib thread has pushed the context as thread-default.
	// This prevents the P1 race where g_main_context_invoke() from the app
	// thread could direct-dispatch before the loop runs.
	WaitForContextReady();

	fRunning = true;

	log_info("ProcessManager: GLib loop started on thread %lu", fThread);
}


void
ProcessManager::StopGLibLoop()
{
	if (!fRunning)
		return;

	g_main_loop_quit(fLoop);
	pthread_join(fThread, nullptr);
	g_main_loop_unref(fLoop);

	fLoop = nullptr;
	fContext = nullptr;
	fRunning = false;

	log_info("ProcessManager: GLib loop stopped");
}


void
ProcessManager::SignalContextReady()
{
	fContextReady.store(true, std::memory_order_release);
	fStartCond.notify_one();
}


bool
ProcessManager::WaitForContextReady()
{
	std::unique_lock<std::mutex> lock(fStartMutex);
	fStartCond.wait(lock, [this]() {
		return fContextReady.load(std::memory_order_acquire);
	});
	return true;
}


void
ProcessManager::InvokeOnGLibThread(GSourceFunc func, void* data)
{
	g_main_context_invoke(fContext, func, data);
}


void
ProcessManager::InvokeOnGLibThreadSync(std::function<void()> func)
{
	if (g_main_context_is_owner(fContext)) {
		func();
		return;
	}

	struct Call {
		std::function<void()>	func;
		std::mutex				lock;
		std::condition_variable	done;
		bool					finished = false;
	} call;
	call.func = std::move(func);

	g_main_context_invoke(fContext, [](void* data) -> gboolean {
		Call* call = static_cast<Call*>(data);
		call->func();
		std::lock_guard<std::mutex> _(call->lock);
		call->finished = true;
		call->done.notify_one();
		return G_SOURCE_REMOVE;
	}, &call);

	std::unique_lock<std::mutex> lock(call.lock);
	call.done.wait(lock, [&call]() { return call.finished; });
}
