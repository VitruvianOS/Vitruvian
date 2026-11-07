/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SUPPORT_USER_SEMAPHORE_H
#define _SUPPORT_USER_SEMAPHORE_H


#include <OS.h>
#include <SupportDefs.h>


/* Process-local counting semaphore on caller storage, with a FIFO waiter
 * queue. A waiter that lost a unit to a newcomer is handed the next one
 * directly. */

enum {
	USER_SEM_WAITING = 0,
	USER_SEM_WOKEN = 1,		// woken without a unit
	USER_SEM_GRANTED = 2,	// owns a unit
	USER_SEM_DELETED = 3
};


struct user_sem_waiter {
	user_sem_waiter*	next;
	int32				state;
	int32				overtaken;	// lost a unit to a newcomer
};


/* A deleted semaphore has count -1. user_sem_init leaves spare alone. */
struct user_sem {
	int32				lock;	// 0 free, 1 held, 2 contended
	int32				count;
	int32				inside;	// waiters between enqueue and their last access
	int32				spare;	// free for the owner
	user_sem_waiter*	queue;
};


#ifdef __cplusplus
extern "C" {
#endif

void		user_sem_init(user_sem* sem, int32 count);
status_t	user_sem_acquire(user_sem* sem, bigtime_t timeout);
void		user_sem_release(user_sem* sem);
void		user_sem_delete(user_sem* sem);

#ifdef __cplusplus
}
#endif


#endif	// _SUPPORT_USER_SEMAPHORE_H
