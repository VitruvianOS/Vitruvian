/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <support/UserSemaphore.h>

#include <support/BranchHints.h>

#include <libroot/futex.h>


static void
lock_sem(user_sem* sem)
{
	int32 c = 0;
	if (__atomic_compare_exchange_n(&sem->lock, &c, 1, false,
			__ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
		return;
	}

	if (c != 2)
		c = __atomic_exchange_n(&sem->lock, 2, __ATOMIC_ACQUIRE);
	while (c != 0) {
		futex_wait(&sem->lock, 2, 0);
		c = __atomic_exchange_n(&sem->lock, 2, __ATOMIC_ACQUIRE);
	}
}


static void
unlock_sem(user_sem* sem)
{
	if (__atomic_fetch_sub(&sem->lock, 1, __ATOMIC_RELEASE) != 1) {
		__atomic_store_n(&sem->lock, 0, __ATOMIC_RELEASE);
		futex_wake(&sem->lock, 1);
	}
}


static void
unlink_waiter(user_sem* sem, user_sem_waiter* waiter)
{
	for (user_sem_waiter** link = &sem->queue; *link != NULL;
			link = &(*link)->next) {
		if (*link == waiter) {
			*link = waiter->next;
			waiter->next = NULL;
			return;
		}
	}
}


// Leaves the semaphore after a waiter's last access. Unlocks.
static void
leave_sem(user_sem* sem)
{
	sem->inside--;
	if (sem->count < 0 && sem->inside == 0)
		futex_wake(&sem->inside, 1);
	unlock_sem(sem);
}


void
user_sem_init(user_sem* sem, int32 count)
{
	sem->lock = 0;
	sem->count = count;
	sem->inside = 0;
	sem->queue = NULL;
}


status_t
user_sem_acquire(user_sem* sem, bigtime_t timeout)
{
	lock_sem(sem);
	if (unlikely(sem->count < 0)) {
		unlock_sem(sem);
		return B_BAD_SEM_ID;
	}
	if (sem->count > 0
		&& (sem->queue == NULL || !sem->queue->overtaken)) {
		sem->count--;
		unlock_sem(sem);
		return B_OK;
	}
	if (timeout == 0) {
		unlock_sem(sem);
		return futex_timeout_error(timeout);
	}

	user_sem_waiter waiter;
	waiter.next = NULL;
	waiter.state = USER_SEM_WAITING;
	waiter.overtaken = 0;

	user_sem_waiter** tail = &sem->queue;
	while (*tail != NULL)
		tail = &(*tail)->next;
	*tail = &waiter;
	sem->inside++;
	unlock_sem(sem);

	bigtime_t deadline = futex_deadline(timeout);

	for (;;) {
		status_t failure = B_OK;
		while (__atomic_load_n(&waiter.state, __ATOMIC_ACQUIRE)
				== USER_SEM_WAITING) {
			bigtime_t left = futex_time_left(deadline);
			if (left < 0) {
				failure = futex_timeout_error(timeout);
				break;
			}
			status_t error = futex_wait(&waiter.state, USER_SEM_WAITING, left);
			if (futex_wait_failed(error)) {
				failure = error;
				break;
			}
			if (error == B_TIMED_OUT) {
				failure = futex_timeout_error(timeout);
				break;
			}
		}

		lock_sem(sem);
		int32 state = __atomic_load_n(&waiter.state, __ATOMIC_ACQUIRE);
		status_t result = failure;
		if (state == USER_SEM_GRANTED) {
			result = B_OK;
		} else if (state == USER_SEM_DELETED || sem->count < 0) {
			result = B_BAD_SEM_ID;
		} else if (state == USER_SEM_WOKEN) {
			if (sem->count > 0) {
				sem->count--;
				result = B_OK;
			} else if (failure == B_OK) {
				waiter.overtaken = 1;
				__atomic_store_n(&waiter.state, USER_SEM_WAITING,
					__ATOMIC_RELAXED);
				waiter.next = sem->queue;
				sem->queue = &waiter;
				unlock_sem(sem);
				continue;
			}
		} else
			unlink_waiter(sem, &waiter);

		leave_sem(sem);
		return result;
	}
}


void
user_sem_release(user_sem* sem)
{
	lock_sem(sem);
	if (unlikely(sem->count < 0)) {
		unlock_sem(sem);
		return;
	}

	user_sem_waiter* waiter = sem->queue;
	if (waiter == NULL) {
		sem->count++;
		unlock_sem(sem);
		return;
	}

	sem->queue = waiter->next;
	waiter->next = NULL;

	int32 state = USER_SEM_WOKEN;
	if (waiter->overtaken)
		state = USER_SEM_GRANTED;
	else
		sem->count++;

	__atomic_store_n(&waiter->state, state, __ATOMIC_RELEASE);
	futex_wake(&waiter->state, 1);
	unlock_sem(sem);
}


void
user_sem_delete(user_sem* sem)
{
	lock_sem(sem);
	sem->count = -1;

	user_sem_waiter* waiter = sem->queue;
	sem->queue = NULL;
	while (waiter != NULL) {
		user_sem_waiter* next = waiter->next;
		waiter->next = NULL;
		__atomic_store_n(&waiter->state, USER_SEM_DELETED, __ATOMIC_RELEASE);
		futex_wake(&waiter->state, 1);
		waiter = next;
	}

	// Waiters leave under the lock, so none touches the semaphore after this.
	while (sem->inside > 0) {
		int32 inside = sem->inside;
		unlock_sem(sem);
		futex_wait(&sem->inside, inside, 0);
		lock_sem(sem);
	}
	unlock_sem(sem);
}
