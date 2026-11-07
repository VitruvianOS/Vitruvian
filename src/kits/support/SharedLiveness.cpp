/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <support/SharedLiveness.h>

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include <libroot/futex.h>


/* The wait watches glibc's robust mutex lock word, which is the first int
 * of the pthread_mutex_t. The kernel robust list code uses the same word. */
static_assert(offsetof(pthread_mutex_t, __data.__lock) == 0,
	"liveness: the lock word must be the first int of pthread_mutex_t");

#ifndef SYS_futex_waitv
#	define SYS_futex_waitv 449
#endif

#define LIVENESS_FUTEX_WAITERS		((uint32_t)0x80000000)
#define LIVENESS_FUTEX_OWNER_DIED	((uint32_t)0x40000000)
#define LIVENESS_FUTEX_TID_MASK		((uint32_t)0x3fffffff)
#define LIVENESS_FUTEX_32			0x02

struct liveness_waitv {
	uint64_t	val;
	uint64_t	uaddr;
	uint32_t	flags;
	uint32_t	reserved;
};


static volatile uint32_t*
lock_word(shared_liveness_state* state)
{
	return (volatile uint32_t*)&state->mutex;
}


// One holder thread per team locks every registered mutex, so the mutexes
// are owned by a thread that lives as long as the team.
enum {
	REQUEST_HOLD,
	REQUEST_RELEASE
};

struct holder_request {
	holder_request*			next;
	shared_liveness_state*	state;
	int						kind;
	bool					done;
	int						error;
};

static pthread_mutex_t sHolderLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sHolderCondition = PTHREAD_COND_INITIALIZER;
static holder_request* sRequests = NULL;
static bool sHolderStarted = false;
static pid_t sHolderPid = 0;


// A forked child does not have the holder thread and may have inherited a
// locked mutex, so it starts from scratch.
static void
reset_after_fork()
{
	pid_t pid = getpid();
	if (sHolderPid == pid)
		return;

	pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
	sHolderLock = lock;
	sHolderCondition = condition;
	sRequests = NULL;
	sHolderStarted = false;
	sHolderPid = pid;
}


static void*
holder_thread(void*)
{
	pthread_mutex_lock(&sHolderLock);
	for (;;) {
		while (sRequests == NULL)
			pthread_cond_wait(&sHolderCondition, &sHolderLock);

		holder_request* request = sRequests;
		sRequests = request->next;

		if (request->kind == REQUEST_HOLD) {
			int error = pthread_mutex_lock(&request->state->mutex);
			if (error == EOWNERDEAD) {
				pthread_mutex_consistent(&request->state->mutex);
				error = 0;
			}
			if (error == 0) {
				__atomic_store_n(&request->state->armed, 1,
					__ATOMIC_SEQ_CST);
				futex_wake_all_shared(&request->state->armed);
			}
			request->error = error;
		} else {
			// Disarm first, so the unlock is never seen as a death.
			__atomic_store_n(&request->state->armed, 0, __ATOMIC_SEQ_CST);
			request->error = pthread_mutex_unlock(&request->state->mutex);
			futex_wake_all_shared(&request->state->armed);
		}

		request->done = true;
		pthread_cond_broadcast(&sHolderCondition);
	}
	return NULL;
}


static status_t
submit_request(shared_liveness_state* state, int kind)
{
	pthread_mutex_lock(&sHolderLock);
	reset_after_fork();

	if (!sHolderStarted) {
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		pthread_t thread;
		int error = pthread_create(&thread, &attr, holder_thread, NULL);
		pthread_attr_destroy(&attr);
		if (error != 0) {
			pthread_mutex_unlock(&sHolderLock);
			return B_NO_MEMORY;
		}
		sHolderStarted = true;
	}

	holder_request request;
	request.next = NULL;
	request.state = state;
	request.kind = kind;
	request.done = false;
	request.error = 0;

	holder_request** link = &sRequests;
	while (*link != NULL)
		link = &(*link)->next;
	*link = &request;
	pthread_cond_broadcast(&sHolderCondition);

	while (!request.done)
		pthread_cond_wait(&sHolderCondition, &sHolderLock);
	pthread_mutex_unlock(&sHolderLock);

	return request.error == 0 ? B_OK : B_ERROR;
}


status_t
shared_liveness_init(shared_liveness_state* state)
{
	if (state == NULL)
		return B_BAD_VALUE;

	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
	pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
	int error = pthread_mutex_init(&state->mutex, &attr);
	pthread_mutexattr_destroy(&attr);
	if (error != 0)
		return B_ERROR;

	state->armed = 0;
	state->_reserved[0] = state->_reserved[1] = state->_reserved[2] = 0;
	return B_OK;
}


status_t
shared_liveness_hold(shared_liveness_state* state)
{
	if (state == NULL)
		return B_BAD_VALUE;
	return submit_request(state, REQUEST_HOLD);
}


void
shared_liveness_release(shared_liveness_state* state)
{
	if (state != NULL
		&& __atomic_load_n(&state->armed, __ATOMIC_ACQUIRE) != 0) {
		submit_request(state, REQUEST_RELEASE);
	}
}


bool
shared_liveness_peer_dead(shared_liveness_state* state)
{
	if (state == NULL)
		return false;
	return (__atomic_load_n(lock_word(state), __ATOMIC_ACQUIRE)
		& LIVENESS_FUTEX_OWNER_DIED) != 0;
}


static status_t
wait_two(volatile int32* word, int32 expected, volatile uint32_t* second,
	uint32_t secondExpected, bigtime_t timeoutUs)
{
	static int32 sNoWaitv = 0;
	if (__atomic_load_n(&sNoWaitv, __ATOMIC_RELAXED) != 0)
		return futex_wait_shared(word, expected, timeoutUs);

	liveness_waitv waiters[2];
	waiters[0].val = (uint32_t)expected;
	waiters[0].uaddr = (uintptr_t)word;
	waiters[0].flags = LIVENESS_FUTEX_32;
	waiters[0].reserved = 0;
	waiters[1].val = secondExpected;
	waiters[1].uaddr = (uintptr_t)second;
	waiters[1].flags = LIVENESS_FUTEX_32;
	waiters[1].reserved = 0;

	struct timespec ts;
	struct timespec* tsp = NULL;
	if (timeoutUs > 0) {
		clock_gettime(CLOCK_MONOTONIC, &ts);
		ts.tv_sec += timeoutUs / 1000000;
		ts.tv_nsec += (timeoutUs % 1000000) * 1000;
		if (ts.tv_nsec >= 1000000000) {
			ts.tv_sec++;
			ts.tv_nsec -= 1000000000;
		}
		tsp = &ts;
	}

	long result = syscall(SYS_futex_waitv, waiters, 2, 0, tsp,
		CLOCK_MONOTONIC);
	if (result >= 0)
		return B_OK;
	switch (errno) {
		case ENOSYS:
			__atomic_store_n(&sNoWaitv, 1, __ATOMIC_RELAXED);
			return futex_wait_shared(word, expected, timeoutUs);
		case ETIMEDOUT:
			return B_TIMED_OUT;
		case EINTR:
			return B_INTERRUPTED;
		case EAGAIN:
			return B_BUSY;
		default:
			return B_ERROR;
	}
}


status_t
shared_liveness_wait(shared_liveness_state* peer, volatile int32* word,
	int32 expected, bigtime_t timeoutUs)
{
	if (peer == NULL)
		return futex_wait_shared(word, expected, timeoutUs);

	volatile uint32_t* lock = lock_word(peer);
	for (;;) {
		uint32_t value = __atomic_load_n(lock, __ATOMIC_ACQUIRE);
		if ((value & LIVENESS_FUTEX_OWNER_DIED) != 0) {
			// The kernel wakes one waiter, pass it on.
			futex_wake_all_shared((volatile int32*)lock);
			return B_BAD_SEM_ID;
		}

		if (__atomic_load_n(&peer->armed, __ATOMIC_ACQUIRE) == 0) {
			// No holder yet or it left: wake on the arming instead.
			return wait_two(word, expected, (volatile uint32_t*)&peer->armed,
				0, timeoutUs);
		}

		uint32_t owner = value & LIVENESS_FUTEX_TID_MASK;
		if (owner == 0)
			continue;	// released between the two loads

		// The kernel wakes on owner death only when WAITERS is set.
		if ((value & LIVENESS_FUTEX_WAITERS) == 0) {
			uint32_t desired = value | LIVENESS_FUTEX_WAITERS;
			if (!__atomic_compare_exchange_n(lock, &value, desired, false,
					__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
				continue;
			}
			value = desired;
		}

		return wait_two(word, expected, lock, value, timeoutUs);
	}
}
