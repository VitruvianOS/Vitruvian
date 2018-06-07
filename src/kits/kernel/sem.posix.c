//------------------------------------------------------------------------------
//	Copyright (c) 2004, Bill Hayden
//
//	Permission is hereby granted, free of charge, to any person obtaining a
//	copy of this software and associated documentation files (the "Software"),
//	to deal in the Software without restriction, including without limitation
//	the rights to use, copy, modify, merge, publish, distribute, sublicense,
//	and/or sell copies of the Software, and to permit persons to whom the
//	Software is furnished to do so, subject to the following conditions:
//
//	The above copyright notice and this permission notice shall be included in
//	all copies or substantial portions of the Software.
//
//	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//	DEALINGS IN THE SOFTWARE.
//
//	File Name:		sem.c
//	Author:			Bill Hayden <hayden@haydentech.com>
//	Description:	Implements BeOS semaphores code via SysV sem calls
//
//------------------------------------------------------------------------------


/*
 
Important concepts:
POSIX named semaphore are created as a one-to-one representation of a
Be semaphore.  The name of the POSIX semaphore is the sem_id of the Be
semaphore.  The Be semaphore name is stored as an attribute on the
POSIX semaphore's filesystem object (in /dev/shm/sem typically).

POSIX does not specify the ability to do multiple acquires or releases,
so we must include additional library code which handles these cases.

*/

#include <OS.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>

/* Use XOPEN2K extensions to pick up sem_timedwait() from semaphore.h */
#ifndef __USE_XOPEN2K
#define __USE_XOPEN2K
#endif /* __USE_XOPEN2K */
#include <semaphore.h>

#define TRACE_SEM 1
#if TRACE_SEM
#	define TRACE(x) printf x
#else
#	define TRACE(x) ;
#endif


/* Decrement *MEM if it is > 0, and return the old value.  */
#ifndef atomic_subtract_if_positive
#define atomic_subtract_if_positive(mem, amount)	\
	({ __typeof (*(mem)) __oldval;					\
	__typeof (mem) __memp = (mem);					\
													\
	do												\
	{												\
		__oldval = *__memp;							\
		if (__builtin_expect (__oldval <= (amount - 1), 0))	\
			break;									\
	}												\
	while (__builtin_expect (atomic_compare_and_exchange_bool_acq (__memp,   \
									__oldval - amount,	\
									__oldval),		\
				0));\
	__oldval; })
#endif


static int get_sem_id();
static sem_t* get_posix_sem(int id);
static void construct_sem_timeout(struct timespec* tmout,
								  uint32 flags,
								  bigtime_t raw_timeout);

static sem_t* sem_admin = SEM_FAILED;
const char* sem_admin_name = "/cosmoe-sem-admin";

static sem_t* sem_count = SEM_FAILED;
const char* sem_count_name = "/cosmoe-sem-count";

const char* sem_name_pattern = "/cosmoe-sem-%d";


sem_id create_sem_etc(int32 count,
					  const char *name,
					  team_id owner)
{
	int id;
	int err;
	char sem_name[64];
	
	TRACE(("create_sem_etc: enter\n"));
	
	if ((count < 0) || (count >= SEM_VALUE_MAX))
		return B_BAD_VALUE;
	
	// request an id for the new semaphore
	id = get_sem_id();
	
	if (snprintf(sem_name, 63, sem_name_pattern, id) == -1)
	{
		TRACE(("create_sem_etc(): could not create name!\n"));
		return B_NO_MORE_SEMS;
	};
	
	if (SEM_FAILED == sem_open(sem_name, O_CREAT | O_EXCL, 0755, count))
	{
		TRACE(("create_sem_etc(): could not create a POSIX sem!\n"));
		return B_NO_MORE_SEMS;
	}
	
	return id;
}


sem_id create_sem(int32 count,
				  const char *name)
{
	return create_sem_etc(count, name, getpid());
}


status_t delete_sem(sem_id id)
{
	return delete_sem_etc(id, 0, false);
}


status_t delete_sem_etc(sem_id id,
						status_t return_code,
						bool interrupted)
{
	int err;
	int count;
	sem_t* sem;

	TRACE(("delete_sem_etc(%ld): enter\n", id));
	
	// Check for bad sem_id
	if (id < 0)
		return B_BAD_SEM_ID;
		
	sem = get_posix_sem(id);
	
	if (sem == SEM_FAILED)
		return B_BAD_SEM_ID;

	// POSIX sems cannot be destroyed if there is a waiting thread.
	// A sensible implementation would handle this case for you.
	err = sem_destroy(sem);
	while (err < 0 && errno == EBUSY)
	{
		TRACE(("delete_raw_sem(): a thread was waiting!\n"));
		sem_post(sem);
		err = sem_destroy(sem);
	}
	
	// If there was a non-EBUSY error, we can only assume that we
	// did not have rights to delete the sem
	if (err == -1)
		return B_BAD_SEM_ID;

	return B_OK;
}


status_t acquire_sem(sem_id id)
{
	return acquire_sem_etc(id, 1, 0, 0);
}


status_t acquire_sem_etc(sem_id id,
						 int32 count,
						 uint32 flags,
						 bigtime_t timeout)
{
	struct timespec tmout;
	int err;
	sem_t* sem;
	bool trywait = false;

	TRACE(("acquire_sem_etc(%ld): enter\n", id));

	// Check for bad sem_id
	if (id < 0)
		return B_BAD_SEM_ID;
	
	// Check for invalid count
	if ((count < 0) || (count >= SEM_VALUE_MAX))
		return B_BAD_VALUE;
	
	sem = get_posix_sem(id);
	if (sem == SEM_FAILED)
		return B_BAD_SEM_ID;
	
	// If we have a zero timeout, don't wait for success
	if ((flags & B_TIMEOUT) && (timeout <= 0))
	{
		trywait = true;
		err = sem_trywait_etc(sem, count);
	}
	else if (flags & B_TIMEOUT)
	{
		construct_sem_timeout(&tmout, flags, timeout);
		err = sem_timedwait_etc(sem, &tmout, count);
	}
	else
	{
		err = sem_wait_etc(sem, count);
	}
	
	// Convert the POSIX error, if any, to a B_* error
	if (err < 0)
	{
		if ((errno == ETIMEDOUT) || (errno == EAGAIN))
			err = (trywait) ? B_WOULD_BLOCK : B_TIMED_OUT;
		else if (errno == EINTR)
			err = B_INTERRUPTED;
		else
		{
			TRACE(("acquire_sem_etc(): undefined error %d, errno is %d\n", err, errno));
			err = B_ERROR;
		}
	}
	
	return err;
}


status_t release_sem(sem_id id)
{
	return release_sem_etc(id, 1, 0);
}


status_t release_sem_etc(sem_id id,
						 int32 count,
						 uint32 flags)
{
	sem_t* sem;

	TRACE(("release_sem_etc(%ld): enter\n", id));

	// Check for bad sem_id
	if (id < 0)
		return B_BAD_SEM_ID;

	// Check for invalid count
	if ((count <= 0) || (count >= SEM_VALUE_MAX))
		return B_BAD_VALUE;

	sem = get_posix_sem(id);
	if (sem == SEM_FAILED)
		return B_BAD_SEM_ID;
	
	while (count--)
		sem_post(sem);

	return B_OK;
}


status_t get_sem_count(sem_id id,
					   int32 *thread_count)
{
	int semcount;
	sem_t * sem;
	
	TRACE(("get_sem_count(%ld): enter\n", id));
	
	// Check for bad sem_id
	if (id < 0)
		return B_BAD_SEM_ID;
		
	sem = get_posix_sem(id);
	if (sem == SEM_FAILED)
		return B_BAD_SEM_ID;
		
	// If thread_count is valid, set it
	if (thread_count)
	{
		sem_getvalue(sem, &semcount);
		*thread_count = semcount;
	}

	return B_OK;
}


status_t _get_sem_info(sem_id id,
					   struct sem_info *info,
					   size_t size)
{
	sem_t* sem;
	int semcount;
	
	TRACE(("_get_sem_info(%ld): enter\n", id));

	// Check for bad sem_id
	if (id < 0)
		return B_BAD_SEM_ID;
	
	if (info == NULL || size != sizeof(sem_info))
		return B_BAD_VALUE;
		
	sem = get_posix_sem(id);

	info->sem = id;
	info->team = getpid();
	strcpy(info->name, "unnamed sem");
	sem_getvalue(sem, &semcount);
	info->count = semcount;
	info->latest_holder	= 0;
	
	return B_OK;
}

status_t _get_next_sem_info(team_id team,
							int32 *_cookie,
							struct sem_info *info,
							size_t size)
{
	TRACE(("_get_next_sem_info(): enter\n"));
	
	/* no-op */
	
	return B_BAD_VALUE;
}

status_t set_sem_owner(sem_id id,
					   team_id team)
{
	TRACE(("set_sem_owner(%ld): enter\n", id));

	// Check for bad sem_id
	if (id < 0)
		return B_BAD_SEM_ID;
	
	/* not supported (or needed) under Cosmoe */
	
	return B_OK;
}


void construct_sem_timeout(struct timespec* ts, uint32 flags, bigtime_t timeout)
{
	// POSIX wants an absolute timeout value
	if (flags & B_ABSOLUTE_TIMEOUT)
	{
		/* We already have what we need, just convert it */
		ts->tv_sec = timeout / 1000000LL;
		ts->tv_nsec = (timeout % 1000000LL) * 1000L;
	}
	else /* B_RELATIVE_TIMEOUT */
	{
		/* We need to turn this relative time into an absolute one */
		struct timeval now;
		gettimeofday(&now, NULL);
		ts->tv_sec = now.tv_sec + (timeout / 1000000LL);
		ts->tv_nsec = (now.tv_usec + (timeout % 1000000LL)) * 1000L;
	}

	/* If we ended up with an overflow in tv_nsec, spill it into tv_sec */
	while (ts->tv_nsec >= 1000000000L)
	{
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000L;
	}
}


/*
ADMIN_COUNT_SEM: pseudo-sem that holds the ID of the next sem to create
ADMIN_SEM_SEM:   sole purpose is to protect the count sem
*/
#define ADMIN_COUNT_SEM 0
#define ADMIN_SEM_SEM   1
#define ADMIN_AREA_SEM  2

int get_sem_id()
{
	int id;
	int err;

	TRACE(("get_sem_id: enter\n"));

	// See if this app already knows about the administrative sem
	if (sem_admin == SEM_FAILED)
		init_admin_sem();
	
	// See if this app already knows about the count sem
	if (sem_count == SEM_FAILED)
		init_count_sem();
	
	// Grab the admin sem, so we can safely play with the count sem
	sem_wait(sem_admin);
	
	// Read the value of the count sem into id, then increment
	sem_getvalue(sem_count, &id);
	sem_post(sem_count);
	
	// Release the admin sem
	sem_post(sem_admin);
	
	// Return the read value
	TRACE(("get_sem_id: returned id %d\n", id));
	return id;
}


static sem_t* get_posix_sem(int id)
{
	char sem_name[64];
	
	TRACE(("create_sem_etc: enter\n"));
		
	if (snprintf(sem_name, 63, "/cosmoe-sem-%d", id) == -1)
	{
		TRACE(("create_sem_etc(): could not create name!\n"));
		return B_NO_MORE_SEMS;
	};
	
	return sem_open(sem_name, 0);
}


static int dump_sem_list(void)
{
	int err;
	
	if (sem_count == SEM_FAILED)
		init_count_sem();
	
	err = sem_getvalue(sem_count, &maxsems);
	if (err != -1)
	{
		int id, count;
		
		for (id = 0; id < maxsems; id++) 
		{
			sem_t* sem = get_posix_sem(id);
			
			if (sem == SEM_FAILED)
				continue;
			
			err = sem_getvalue(sem, &count);
			if (err != -1)
				printf("id: %d\t\tcount: %d\n", id, count);
		}
	}
	
	return 0;
}


static void dump_sem(int id)
{
	int maxsems;
	 
	sem_getvalue(sem_count, &maxsems);
	
	if ((id < maxsems) && (id >= 0))
	{
		int count;
		sem_t* sem;
		int err == -1;
		
		sem = get_posix_sem(id);
		
		if (sem != SEM_FAILED)
			err = sem_getvalue(sem, &count);
		
		if (err != -1)
			printf("id: %d\t\tcount: %d\n", id, count);
		else
			printf("There is no active semaphore with that ID.\n");
	}
	else
		printf("There is no active semaphore with that ID.\n");
}

int dump_sem_info(int argc, char **argv)
{
	if (argc < 2)
		dump_sem_list();
	else
		dump_sem(atoi(argv[1]));

	return 0;
}


static void init_admin_sem(void)
{
	// Try to create the admin sem, inited to 1
	sem_admin = sem_open(sem_admin_name, O_CREAT | O_EXCL, 0777, 1);
	
	if (sem_admin == SEM_FAILED)
	{
		// It must already exist, so attach to it
		sem_admin = sem_open(sem_admin_name, 0);
		if (sem_admin == SEM_FAILED)
		{
			// Still can't get it... we are toast
			TRACE(("get_sem_id: FATAL(%d): no admin sem, abandon all hope\n", errno));
		}
	}
}


static void init_count_sem(void)
{
	// Try to create the count sem, inited to 0
	sem_count = sem_open(sem_count_name, O_CREAT | O_EXCL, 0777, 0);
	
	if (sem_count == SEM_FAILED)
	{
		// It must already exist, so attach to it
		sem_count = sem_open(sem_count_name, 0);
		if (sem_count == SEM_FAILED)
		{
			// Still can't get it... we are toast
			TRACE(("get_sem_id: FATAL(%d): no count sem, abandon all hope\n", errno));
		}
	}
}


static int sem_trywait_etc(sem_t *sem, int count)
{
	int *futex = (int *) sem;
	int val;
	
	if (*futex > 0)
	{
		val = atomic_subtract_if_positive(futex, count);
		if (val > 0)
		return 0;
	}
	
	errno = EAGAIN;
	return -1;
}


int sem_wait_etc(sem_t *sem, int count)
{
	/* First check for cancellation.  */
	CANCELLATION_P (THREAD_SELF);
	
	int *futex = (int *) sem;
	int err;
	
	do
	{
		if (atomic_subtract_if_positive(futex, count) > 0)
			return 0;
	
		/* Enable asynchronous cancellation.  Required by the standard.  */
		int oldtype = __pthread_enable_asynccancel ();
	
		err = lll_futex_wait(futex, 0);
	
		/* Disable asynchronous cancellation.  */
		__pthread_disable_asynccancel (oldtype);
	}
	while (err == 0 || err == -EWOULDBLOCK);
	
	errno = -err;
	return -1;
}

int
sem_timedwait_etc(sem_t *sem, const struct timespec *abstime, int count)
{
	/* First check for cancellation.  */
	CANCELLATION_P (THREAD_SELF);
	
	int *futex = (int *) sem;
	int val;
	int err;
	
	if (*futex > 0)
	{
		val = atomic_subtract_if_positive(futex, count);
		if (val > 0)
		return 0;
	}
	
	err = -EINVAL;
	if (abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000)
		goto error_return;
	
	do
	{
		struct timeval tv;
		struct timespec rt;
		int sec, nsec;
	
		/* Get the current time.  */
		__gettimeofday (&tv, NULL);
	
		/* Compute relative timeout.  */
		sec = abstime->tv_sec - tv.tv_sec;
		nsec = abstime->tv_nsec - tv.tv_usec * 1000;
		if (nsec < 0)
		{
			nsec += 1000000000;
			--sec;
		}
	
		/* Already timed out?  */
		err = -ETIMEDOUT;
		if (sec < 0)
			goto error_return;
	
		/* Do wait.  */
		rt.tv_sec = sec;
		rt.tv_nsec = nsec;
	
		/* Enable asynchronous cancellation.  Required by the standard.  */
		int oldtype = __pthread_enable_asynccancel ();
	
		err = lll_futex_timed_wait (futex, 0, &rt);
	
		/* Disable asynchronous cancellation.  */
		__pthread_disable_asynccancel (oldtype);
	
		if (err != 0 && err != -EWOULDBLOCK)
			goto error_return;
	
		val = atomic_subtract_if_positive(futex, count);
	}
	while (val <= 0);
	
	return 0;
	
	error_return:
	__set_errno (-err);
	return -1;
}
