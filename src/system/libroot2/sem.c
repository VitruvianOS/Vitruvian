//------------------------------------------------------------------------------
//	Copyright (c) 2004, Bill Hayden
//	Copyright (c) 2018-2019, Dario Casalinuovo
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
SystemV semaphores are allocated in groups of SEMMSL (usually 250).  We
map the Cosmoe sem_id to a group and group member.  Groups are not created
until needed, and are currently never deleted until Cosmoe shuts down and
clean_shm.sh is run.  Each member of the semaphore group is set to an
initial value of SEMVMX as a marker signifying that it is unused.  When
it comes into use via a call to create_sem_etc the sem value is changed
to the specified count.  If by some fluke an active sem achieves a value
of SEMVMX (usually 32767), it would be considered unused in this scheme.

TODO:
Add name and owner support using shared memory, locked by a sem
in the administrative group (ADMIN_AREA_SEM).

*/

#include <OS.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>

/* Use GNU extensions to pick up semtimedop() from sem.h */
#ifndef __USE_GNU
#define __USE_GNU
#endif /* __USE_GNU */
#include <sys/sem.h>

//#define TRACE_SEM 0
#if TRACE_SEM
#	define TRACE(x) printf x
#else
#	define TRACE(x) ;
#endif

#ifndef SEMMSL
#define SEMMSL 250
#endif /* SEMMSL */

#ifndef SEMVMX
#define SEMVMX 32767
#endif /* SEMVMX */

#if defined(_SEM_SEMUN_UNDEFINED)
union semun
{
	int val;					// value for SETVAL
	struct semid_ds *buf;		// buffer for IPC_STAT & IPC_SET
	unsigned short int *array;	// array for GETALL & SETALL
	struct seminfo *__buf;		// buffer for IPC_INFO
};
#endif

typedef union semun sem_union_t;


static int get_sem_id();
static int get_group(int id);
static void construct_sem_timeout(struct timespec* tmout,
								  uint32 flags,
								  bigtime_t raw_timeout);

static int sem_admin_group = -1;


sem_id
create_sem_etc(int32 count, const char *name, team_id owner)
{
	int id = get_sem_id();
	sem_union_t semopts;
	int group;
	int member = id % SEMMSL;
	int err;
	
	TRACE(("create_sem_etc: enter\n"));
	
	if ((count < 0) || (count >= SEMVMX))
		return B_BAD_VALUE;
	
	// If member is zero, then we need to make a new group
	if (member == 0) {
		// /dev/zero chosen for no particular good reason
		key_t key = ftok("/dev/zero", id / SEMMSL);
		unsigned short int array[SEMMSL];
		
		TRACE(("create_sem_etc(): creating sem group %d\n", id / SEMMSL));
		
		// Fill in the array before we even create the group to narrow
		// the window between the semget and the semctl
		for (uint32 x = 0; x < SEMMSL; x++) {
			// SEMVMX, as a sem value, signifies that it is inactive
			array[x] = SEMVMX;
		}
		semopts.array = array;
		
		// Create a new semaphore set
		group = semget(key, SEMMSL, IPC_CREAT | IPC_EXCL | 0700);
		if (group == -1) {
			TRACE(("create_sem_etc(): failed to create new sem group %d!\n", group));
			return B_NO_MORE_SEMS;
		}

		err = semctl(group, 0 /* ignored */, SETALL, semopts);
		if (err < 0) {
			TRACE(("create_sem_etc(): semctl SETALL returned %d!\n", errno));
			// We are especially screwed here because we've already
			// created the sem group, but can't initialize it.
			// Perhaps we should delete it in this case and return
			// B_NO_MORE_SEMS?
		}
	} else {
		group = get_group(id / SEMMSL);
		if (group == -1)
			return B_NO_MORE_SEMS;
	}

#if TRACE_SEM
	// Check to see if it has the "unused" flag value
	if (SEMVMX != semctl(group, member, GETVAL, 0)) {
		TRACE(("create_sem_etc(): using an uninitialized sem!\n"));
	}

#endif

	semopts.val = count;
	err = semctl(group, member, SETVAL, semopts);
	if (err < 0) {
		TRACE(("create_sem_etc(): semctl SETVAL returned %d!\n", errno));
		return B_NO_MORE_SEMS;
	}

	return id;
}


sem_id
_kern_create_sem(int32 count, const char *name)
{
	return create_sem_etc(count, name, getpid());
}


status_t
_kern_delete_sem(sem_id id)
{
	int group = get_group(id / SEMMSL);
	int member = id % SEMMSL;
	sem_union_t semopts;
	int err;
	int count;

	TRACE(("delete_sem_etc(%ld): enter\n", id));
	
	// Check for bad sem_id
	if
	(
		(id < 0)		// invalid sem_id
		||
		(group == -1)	// non-existant group
		||
		(SEMVMX == semctl(group, member, GETVAL, 0)) // sem not inited yet
	)
		return B_BAD_SEM_ID;

	// FIXME: According to the BeBook, we should also check that the
	// current thread belongs to the sem's owning team.  Since we
	// don't yet implement sem ownership, we can't do this yet.
	// This means that currently you can delete any sem you choose.

	// In case threads were waiting on this sem, it may be
	// immediately decremented, so reset the sem to SEMVMX
	do
	{
		// SEMVMX, as a sem value, signifies that it is inactive
		semopts.val = SEMVMX;
		err = semctl(group, member, SETVAL, semopts);
		if (err == -1)
			return B_BAD_SEM_ID;
			
		count = semctl(group, member, GETVAL, 0);
	}
	while (count >= 0 && count != SEMVMX);

	return B_OK;
}


status_t
_kern_acquire_sem(sem_id id)
{
	return acquire_sem_etc(id, 1, 0, 0);
}


status_t
_kern_acquire_sem_etc(sem_id id, uint32 count, uint32 flags,
	bigtime_t timeout)
{
	int group = get_group(id / SEMMSL);
	int member = id % SEMMSL;
	struct sembuf sem_lock = {member, -count, 0};
	struct timespec tmout;
	int err;

	TRACE(("acquire_sem_etc(%ld): enter\n", id));

	// Check for bad sem_id
	if
	(
		(id < 0)		// invalid sem_id
		||
		(group == -1)	// non-existant group
		||
		(SEMVMX == semctl(group, member, GETVAL, 0)) // sem not inited yet
	)
		return B_BAD_SEM_ID;
	
	// Check for invalid count
	if ((count < 0) || (count >= SEMVMX))
		return B_BAD_VALUE;
	
	// If we have a zero timeout, don't wait for success
	if ((flags & B_TIMEOUT) && (timeout <= 0))
		sem_lock.sem_flg = IPC_NOWAIT;

	// Acquire the semaphore
	if (flags & B_TIMEOUT) {
		construct_sem_timeout(&tmout, flags, timeout);
		err = semtimedop(group, &sem_lock, 1, &tmout);
	} else {
		err = semop(group, &sem_lock, 1);
	}
	
	// Convert the POSIX error, if any, to a B_* error
	if (err < 0) {
		if ((errno == ETIMEDOUT) || (errno == EAGAIN))
			err = (sem_lock.sem_flg == IPC_NOWAIT) ? B_WOULD_BLOCK : B_TIMED_OUT;
		else if (errno == EINTR)
			err = B_INTERRUPTED;
		else {
			TRACE(("acquire_sem_etc(): undefined error %d, errno is %d\n", err, errno));
			err = B_ERROR;
		}
	}
	
	return err;
}


status_t
_kern_release_sem(sem_id id)
{
	return release_sem_etc(id, 1, 0);
}


status_t
_kern_release_sem_etc(sem_id id, uint32 count, uint32 flags)
{
	int group = get_group(id / SEMMSL);
	int member = id % SEMMSL;
	struct sembuf sem_lock = {member, count, 0};
	int err;

	TRACE(("release_sem_etc(%ld): enter\n", id));

	// Check for bad sem_id
	if
	(
		(id < 0)		// invalid sem_id
		||
		(group == -1)	// non-existant group
		||
		(SEMVMX == semctl(group, member, GETVAL, 0)) // sem not inited yet
	)
		return B_BAD_SEM_ID;

	// Check for invalid count
	if ((count <= 0) || (count >= SEMVMX))
		return B_BAD_VALUE;

	// Release the semaphore
	err = semop(group, &sem_lock, 1);
	if (err == -1)
		return B_BAD_SEM_ID;

	return B_OK;
}


status_t
_kern_get_sem_count(sem_id id, int32* thread_count)
{
	int group = get_group(id / SEMMSL);
	int member = id % SEMMSL;
	int semcount;
	
	TRACE(("get_sem_count(%ld): enter\n", id));
	
	// Check for bad sem_id
	if
	(
		(id < 0)		// invalid sem_id
		||
		(group == -1)	// non-existant group
	)
		return B_BAD_SEM_ID;
		
	semcount = semctl(group, member, GETVAL, 0);
	
	if (semcount == SEMVMX)
		return B_BAD_SEM_ID;

	// If thread_count is valid, set it
	if (thread_count) {
		*thread_count = semcount;
	}

	return B_OK;
}


status_t
_kern_get_sem_info(sem_id id, struct sem_info *info, size_t size)
{
	int group = get_group(id / SEMMSL);
	int member = id % SEMMSL;
	
	TRACE(("_get_sem_info(%ld): enter\n", id));

	// Check for bad sem_id
	if
	(
		(id < 0)		// invalid sem_id
		||
		(group == -1)	// non-existant group
		||
		(SEMVMX == semctl(group, member, GETVAL, 0)) // sem not inited yet
	)
		return B_BAD_SEM_ID;

	if (info == NULL || size != sizeof(sem_info))
		return B_BAD_VALUE;

	info->sem = id;
	info->team = getpid();
	strcpy(info->name, "unnamed sem");
	info->count = semctl(group, member, GETVAL, 0);
	info->latest_holder	= 0;
	
	return B_OK;
}


status_t
_kern_get_next_sem_info(team_id team, int32 *_cookie,
	struct sem_info *info, size_t size)
{
	TRACE(("_get_next_sem_info(): enter\n"));
	
	/* no-op */
	
	return B_BAD_VALUE;
}


status_t
_kern_set_sem_owner(sem_id id, team_id team)
{
	int group = get_group(id / SEMMSL);
	int member = id % SEMMSL;

	TRACE(("set_sem_owner(%ld): enter\n", id));

	// Check for bad sem_id
	if
	(
		(id < 0)		// invalid sem_id
		||
		(group == -1)	// non-existant group
		||
		(SEMVMX == semctl(group, member, GETVAL, 0)) // sem not inited yet
	)
		return B_BAD_SEM_ID;
	
	/* no-op */
	
	return B_OK;
}


status_t
_kern_switch_sem(sem_id releaseSem, sem_id id)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_switch_sem_etc(sem_id releaseSem, sem_id id, uint32 count, uint32 flags, bigtime_t timeout)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


void
construct_sem_timeout(struct timespec* ts, uint32 flags, bigtime_t timeout)
{
	if (flags & B_ABSOLUTE_TIMEOUT) {
		/* SysV wants a relative timeout value, so we need */
		/* to turn this absolute time into a relative one  */
		struct timeval now;
		gettimeofday(&now, NULL);
		int64 total_nsec = (timeout * 1000LL) - (now.tv_sec * 1000000000LL) - (now.tv_usec * 1000LL);
		ts->tv_sec = total_nsec / 1000000000LL;
		ts->tv_nsec = total_nsec % 1000000000LL;
	} else /* B_RELATIVE_TIMEOUT */ {
		/* We already have what we need, just convert it */
		ts->tv_sec = timeout / 1000000LL;
		ts->tv_nsec = (timeout % 1000000LL) * 1000L;
	}

	/* If we ended up with an overflow in tv_nsec, spill it into tv_sec */
	while (ts->tv_nsec >= 1000000000L) {
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

int
get_sem_id()
{
	int id;
	int err;

	TRACE(("get_sem_id: enter\n"));

	// See if this app already knows about the administrative sem group
	if (sem_admin_group == -1) {
		sem_union_t semopts;
		key_t key = ftok("/usr/local/bin/", 's');
		
		// Try to create a new administrative sem group
		sem_admin_group = semget(key, 3, IPC_CREAT | IPC_EXCL | 0700);
		if (sem_admin_group != -1) {
			// We created a new sem group, so we must initialize it
			
			// Initialize count sem to zero
			semopts.val = 0;
			err = semctl(sem_admin_group, ADMIN_COUNT_SEM, SETVAL, semopts);

			// Initialize sem sem to one (i.e. unlocked)
			semopts.val = 1;
			err = semctl(sem_admin_group, ADMIN_SEM_SEM, SETVAL, semopts);

			// Initialize area sem to one (i.e. unlocked)
			semopts.val = 1;
			err = semctl(sem_admin_group, ADMIN_AREA_SEM, SETVAL, semopts);
		} else {
			// A sem group already existed, so use that one
			sem_admin_group = semget(key, 2, IPC_CREAT | 0700);
		}
		
		// If we could neither create a new one, nor attach to an existing one...
		if (sem_admin_group == -1) {
			// ...then we are in serious trouble.  The app cannot continue in any
			// reasonable form at this point.
			TRACE(("get_sem_id: FATAL: semget failed (%d), abandon all hope\n", errno));
			return -1;
		}
	}
	
	// Acquire the sem sem
	
	struct sembuf sem_lock = {ADMIN_SEM_SEM, -1, 0};
	err = semop(sem_admin_group, &sem_lock, 1);
	if (err == -1) {
		TRACE(("get_sem_id: semop on ADMIN_SEM_SEM returned %d\n", errno));
	}
	
	// Read the value of the count sem into id
	
	id = semctl(sem_admin_group, ADMIN_COUNT_SEM, GETVAL, 0);
	
	// Increment the count sem
	
	struct sembuf sem_increment = {ADMIN_COUNT_SEM, 1, 0};
	err = semop(sem_admin_group, &sem_increment, 1);
	if (err == -1) {
		TRACE(("get_sem_id: semop on ADMIN_COUNT_SEM returned %d\n", errno));
	}
	
	// Release the sem sem
	
	sem_lock.sem_op = 1;
	err = semop(sem_admin_group, &sem_lock, 1);
	if (err == -1) {
		TRACE(("get_sem_id: semop on ADMIN_SEM_SEM returned %d\n", errno));
	}
	
	// Return the read value
	TRACE(("get_sem_id: returned id %d\n", id));
	return id;
}


static int
get_group(int id)
{
	int group;
	
	key_t key = ftok("/dev/zero", id);
	
	// Create a new semaphore set
	group = semget(key, SEMMSL, 0700);
	if (group == -1) {
		TRACE(("get_group(): failed to find group for group id %d!\n", id));
	}

	return group;
}


static int
dump_sem_list(void)
{
	int id;
	int maxsems = semctl(sem_admin_group, ADMIN_COUNT_SEM, GETVAL, 0);
	int group;
	int member;
	int count;

	for (id = 0; id < maxsems; id++) {
		group = get_group(id / SEMMSL);
		member = id % SEMMSL;
		count = semctl(group, member, GETVAL, 0);
		if ((count != -1) && (count != SEMVMX))
			printf("id: %d\t\tcount: %d\n", id, count);
	}
	return 0;
}


static void
dump_sem(int id)
{
	int maxsems = semctl(sem_admin_group, ADMIN_COUNT_SEM, GETVAL, 0);
	
	if ((id < maxsems) && (id >= 0)) {
		int group = get_group(id / SEMMSL);
		int member = id % SEMMSL;
		int count = semctl(group, member, GETVAL, 0);
		
		if ((count != -1) && (count != SEMVMX))
			printf("id: %d\t\tcount: %d\n", id, count);
		else
			printf("There is no active semaphore with that ID.\n");
	} else
		printf("A semaphore with that ID has never existed.\n");
}

int
dump_sem_info(int argc, char **argv)
{
	if (argc < 2)
		dump_sem_list();
	else
		dump_sem(atoi(argv[1]));

	return 0;
}
