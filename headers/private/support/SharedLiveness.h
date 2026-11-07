/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SHARED_LIVENESS_H
#define _SHARED_LIVENESS_H


#include <pthread.h>

#include <OS.h>
#include <SupportDefs.h>


/* Peer death detection for words in shared memory, done by the kernel.
 *
 * The state sits in memory both peers map. Its creator calls
 * shared_liveness_init(). The peer whose death must be noticed calls
 * shared_liveness_hold(): a per team holder thread locks the robust mutex
 * and keeps it until shared_liveness_release() or the team dies. When the
 * team dies the kernel marks the lock word owner-died and wakes a waiter.
 *
 * A waiter blocks through shared_liveness_wait(), on the word it cares
 * about and on the liveness state at once. It returns B_BAD_SEM_ID when
 * the holder died. See archive/skills/locking.md for the protocol. */
struct shared_liveness_state {
	pthread_mutex_t	mutex;
	// 1 while a holder owns the mutex. Futex word, a waiter that finds it
	// 0 sleeps on it so a later hold wakes the waiter.
	int32			armed;
	int32			_reserved[3];
};


#ifdef __cplusplus
extern "C" {
#endif

status_t	shared_liveness_init(shared_liveness_state* state);
status_t	shared_liveness_hold(shared_liveness_state* state);
void		shared_liveness_release(shared_liveness_state* state);
bool		shared_liveness_peer_dead(shared_liveness_state* state);

/* Waits while *word == expected, like futex_wait_shared(), and also while
 * the holder of peer lives. B_BAD_SEM_ID: the holder died. Other results
 * as futex_wait_shared(); B_OK or B_BUSY mean look at the word again. A
 * NULL peer is a plain futex_wait_shared(). timeoutUs <= 0 waits forever. */
status_t	shared_liveness_wait(shared_liveness_state* peer,
				volatile int32* word, int32 expected, bigtime_t timeoutUs);

#ifdef __cplusplus
}
#endif


#endif	// _SHARED_LIVENESS_H
