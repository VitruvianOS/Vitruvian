// Runs several threads in one test case and reports what they found.
//
// Catch2's assertion macros are not thread safe, so a worker thread must not
// use CHECK or REQUIRE. Instead it uses THREAD_CHECK and THREAD_REQUIRE, which
// record a failure; Run() then reports the recorded failures on the main
// thread once every worker has finished.
//
//		TEST_CASE("BLocker: two threads", "[BLocker][support]")
//		{
//			BLocker locker;
//			ThreadedTest test;
//
//			test.AddThread("A", [&]() {
//				THREAD_REQUIRE(locker.Lock());
//				locker.Unlock();
//			});
//			test.AddThread("B", [&]() { ... });
//
//			test.Run();
//		}
//
// THREAD_REQUIRE stops its own thread, like CPPUNIT_ASSERT did, so a failing
// loop does not report the same failure thousands of times. THREAD_CHECK
// records and carries on. Neither affects the other threads.

#ifndef _THREADED_TEST_H
#define _THREADED_TEST_H

#include <OS.h>

#include <functional>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>


struct ThreadedTestFailure {
	std::string	thread;
	std::string	file;
	int			line;
	std::string	message;
};


namespace ThreadedTestPrivate {

// Thrown by THREAD_REQUIRE to end the thread it is used in. It never leaves
// the thread's entry point.
struct Abort {
};


// What a worker thread records its failures through. Each thread has its own,
// so that the thread name needs no locking.
class Context {
public:
	virtual	~Context() {}

	virtual	void	Record(const char* file, int line,
						const std::string& message) = 0;
};


inline Context*&
CurrentContext()
{
	static thread_local Context* context = NULL;
	return context;
}


inline void
Record(const char* file, int line, const std::string& message, bool fatal)
{
	Context* context = CurrentContext();
	if (context != NULL)
		context->Record(file, line, message);

	if (fatal)
		throw Abort();
}

}	// namespace ThreadedTestPrivate


//! Records a failure and keeps going.
#define THREAD_CHECK(expression)										\
	do {																\
		if (!(expression)) {											\
			ThreadedTestPrivate::Record(__FILE__, __LINE__,				\
				#expression, false);									\
		}																\
	} while (false)

//! Records a failure and ends this thread.
#define THREAD_REQUIRE(expression)										\
	do {																\
		if (!(expression)) {											\
			ThreadedTestPrivate::Record(__FILE__, __LINE__,				\
				#expression, true);										\
		}																\
	} while (false)

//! Records a failure with a message of its own and ends this thread.
#define THREAD_FAIL(message)											\
	ThreadedTestPrivate::Record(__FILE__, __LINE__, (message), true)


class ThreadedTest {
public:
			typedef std::function<void()> Body;

								ThreadedTest()
									:
									fDoneSem(-1)
								{
								}

	//! Adds a thread. It is started by Run(), not here.
			void				AddThread(const std::string& name, Body body)
								{
									fThreads.push_back(Thread(this, name,
										body));
								}

	/*!	Runs every thread, waits for them all, and reports what they recorded
		as Catch2 failures on the calling thread.
	*/
			void				Run();

	/*!	Like Run(), but returns the failures instead of reporting them. Only
		useful for testing this class itself.
	*/
			std::vector<ThreadedTestFailure> Collect();

	//! How long to wait for a thread before giving up, in microseconds.
	static	bigtime_t			Timeout() { return 60000000; }

private:
			struct Thread {
				Thread(ThreadedTest* owner, const std::string& name, Body body)
					:
					owner(owner),
					name(name),
					body(body),
					id(-1)
				{
				}

				ThreadedTest*	owner;
				std::string		name;
				Body			body;
				thread_id		id;
			};

			sem_id				fDoneSem;

			class ThreadContext : public ThreadedTestPrivate::Context {
			public:
				ThreadContext(Thread* thread)
					:
					fThread(thread)
				{
					ThreadedTestPrivate::CurrentContext() = this;
				}

				virtual ~ThreadContext()
				{
					ThreadedTestPrivate::CurrentContext() = NULL;
				}

				virtual void Record(const char* file, int line,
					const std::string& message)
				{
					fThread->owner->_AddFailure(fThread->name, file, line,
						message);
				}

			private:
				Thread*	fThread;
			};

			friend class ThreadContext;

			void				_AddFailure(const std::string& thread,
									const char* file, int line,
									const std::string& message);

	static	int32				_Entry(void* data);

			std::vector<Thread>	fThreads;
			std::vector<ThreadedTestFailure> fFailures;
			std::mutex			fLock;
};


inline void
ThreadedTest::_AddFailure(const std::string& thread, const char* file,
	int line, const std::string& message)
{
	std::lock_guard<std::mutex> locker(fLock);

	ThreadedTestFailure failure;
	failure.thread = thread;
	failure.file = file;
	failure.line = line;
	failure.message = message;
	fFailures.push_back(failure);
}


inline int32
ThreadedTest::_Entry(void* data)
{
	Thread* thread = static_cast<Thread*>(data);

	{
		ThreadContext context(thread);

		try {
			thread->body();
		} catch (const ThreadedTestPrivate::Abort&) {
			// THREAD_REQUIRE failed, and has already been recorded
		} catch (const std::exception& exception) {
			thread->owner->_AddFailure(thread->name, __FILE__, __LINE__,
				std::string("unexpected exception: ") + exception.what());
		} catch (...) {
			thread->owner->_AddFailure(thread->name, __FILE__, __LINE__,
				"unexpected exception");
		}
	}

	release_sem(thread->owner->fDoneSem);
	return 0;
}


inline std::vector<ThreadedTestFailure>
ThreadedTest::Collect()
{
	// Each thread releases fDoneSem as it leaves, and this waits for one
	// release per running thread. wait_for_thread() would be the obvious way
	// to do this, but it currently returns straight away instead of waiting,
	// so the threads would still be running here. The old cppunit harness
	// used a semaphore for the same reason.
	fDoneSem = create_sem(0, "threaded test");
	if (fDoneSem < B_OK) {
		_AddFailure("", __FILE__, __LINE__, "failed to create semaphore");
		return fFailures;
	}

	// Spawned threads start suspended, so they all begin at roughly the same
	// time instead of the first one running to completion before the last one
	// exists.
	int32 running = 0;
	for (size_t i = 0; i < fThreads.size(); i++) {
		Thread& thread = fThreads[i];
		thread.id = spawn_thread(&ThreadedTest::_Entry, thread.name.c_str(),
			B_NORMAL_PRIORITY, &thread);
		if (thread.id < B_OK) {
			_AddFailure(thread.name, __FILE__, __LINE__,
				"failed to spawn thread");
		} else
			running++;
	}

	for (size_t i = 0; i < fThreads.size(); i++) {
		if (fThreads[i].id >= B_OK)
			resume_thread(fThreads[i].id);
	}

	for (int32 i = 0; i < running; i++) {
		status_t result = acquire_sem_etc(fDoneSem, 1, B_RELATIVE_TIMEOUT,
			Timeout());
		if (result != B_OK) {
			// A thread is stuck. Returning would let it keep using memory
			// that is about to go away, so stop here with a clear message
			// rather than corrupting the rest of the run.
			fprintf(stderr, "ThreadedTest: a thread did not finish within "
				"%lld seconds; giving up\n",
				(long long)(Timeout() / 1000000));
			abort();
		}
	}

	delete_sem(fDoneSem);
	fDoneSem = -1;

	return fFailures;
}


inline void
ThreadedTest::Run()
{
	std::vector<ThreadedTestFailure> failures = Collect();

	for (size_t i = 0; i < failures.size(); i++) {
		const ThreadedTestFailure& failure = failures[i];
		UNSCOPED_INFO("in thread \"" << failure.thread << "\" at "
			<< failure.file << ":" << failure.line);
		FAIL_CHECK(failure.message);
	}

	// So that a test case whose checks all live in its threads does not look
	// like it ran no assertions at all
	SUCCEED();
}


#endif	// _THREADED_TEST_H
