/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef VOS_PROCESS_MANAGER_H
#define VOS_PROCESS_MANAGER_H


#include <glib.h>
#include <pthread.h>
#include <atomic>
#include <condition_variable>
#include <mutex>


class ProcessManager {
public:
	static ProcessManager&	Instance();

			void			StartGLibLoop();
			void			StopGLibLoop();
			void			InvokeOnGLibThread(GSourceFunc func, void* data);

			GMainLoop*		MainLoop() const { return fLoop; }
			GMainContext*	Context() const { return fContext; }

	// Called from glib_thread_func to signal context ownership.
	void					SignalContextReady();
	bool					WaitForContextReady();

private:
						ProcessManager();
						~ProcessManager();

						ProcessManager(const ProcessManager&) = delete;
	ProcessManager&	operator=(const ProcessManager&) = delete;

		GMainLoop*		fLoop;
		GMainContext*	fContext;
		pthread_t		fThread;
		std::atomic<bool>	fRunning;
		std::mutex			fStartMutex;
		std::condition_variable fStartCond;
		std::atomic<bool>	fContextReady;
};


#endif // VOS_PROCESS_MANAGER_H
