/*
** Copyright 2004, Bill Hayden. All rights reserved.
** Distributed under the terms of the OpenBeOS License.
*/

#include <sem_internals.h>
#include <stdio.h>
#include <errno.h>
#include <sys/time.h>


#define TRACE_SEM 0
#if TRACE_SEM
#	define TRACE(x) dprintf x
#	define TRACE_BLOCK(x) dprintf x
#else
#	define TRACE(x) ;
#	define TRACE_BLOCK(x) ;
#endif

#define dprintf printf

#if (COSMOE_SEM_STYLE == SYSV_SEMAPHORES)
static int sem_groups[num_sem_groups];
static int sem_lock_groups[num_sem_groups];
#endif


int create_raw_sem(SEM_TYPE* sem, int sem_num, bool unlocked, bool lock_group)
{
	int err;

	TRACE(("create_raw_sem(num = %d)\n", sem_num));

#if (COSMOE_SEM_STYLE == POSIX_SEMAPHORES)
	err = sem_init(sem, 1, unlocked ? 1 : 0);
#endif

#if (COSMOE_SEM_STYLE == SYSV_SEMAPHORES)
	sem_union_t semopts;
	int group = lock_group ? sem_lock_groups[sem_num / SEMMSL] : sem_groups[sem_num / SEMMSL];
	int member = sem_num % SEMMSL;

	semopts.val = unlocked ? 1 : 0;
	err = semctl(group, member, SETVAL, semopts);
	*sem = sem_num;
#endif

	TRACE(("create_raw_sem(): err = %d, errno = %d (%s)\n", err, errno, strerror(errno)));

	return err;
}


status_t acquire_raw_sem(SEM_TYPE* sem, uint32 flags, bigtime_t timeout, bool lock_group)
{
	status_t err;
	struct timespec tmout;

	TRACE(("acquire_raw_sem(): %s timeout\n", (flags & B_TIMEOUT) ? "with" : "no"));

#if (COSMOE_SEM_STYLE == POSIX_SEMAPHORES)
	if (flags & B_TIMEOUT)
	{
		construct_sem_timeout(&tmout, flags, timeout);
		err = sem_timedwait(sem, &tmout);
	}
	else
		err = sem_wait(sem);
#endif


#if (COSMOE_SEM_STYLE == SYSV_SEMAPHORES)
	/* Are we dealing with a sem or the lock on a sem? */
	int group = lock_group ? sem_lock_groups[*sem / SEMMSL] : sem_groups[*sem / SEMMSL];
	int member = *sem % SEMMSL;

	TRACE(("acquire_raw_sem(): before semtimedop = %d\n", semctl(group, member, GETVAL)));

	struct sembuf sem_lock = {member, -1, SEM_UNDO};

	if (flags & B_TIMEOUT)
	{
		construct_sem_timeout(&tmout, flags, timeout);
		TRACE(("acquire_raw_sem(): timeout will occur in %ld.%09ld secs\n", tmout.tv_sec, tmout.tv_nsec));
		err = semtimedop(group, &sem_lock, 1, &tmout);
	}
	else
	{
		err = semop(group, &sem_lock, 1);
	}

	TRACE(("acquire_raw_sem(): after semtimedop = %d\n", semctl(group, member, GETVAL)));
#endif

	TRACE(("acquire_raw_sem(): err = %ld, errno = %d\n", err, errno));

	if (err < 0)
	{
		if ((errno == ETIMEDOUT) || (errno == EAGAIN))
			err = B_TIMED_OUT;
		else if (errno == EINTR)
			err = B_INTERRUPTED;
		else
		{
			TRACE(("acquire_raw_sem(): undefined error - err is %ld, errno is %d\n", err, errno));
			err = B_ERROR;
		}
	}

	return err;
}


int release_raw_sem(SEM_TYPE* sem, bool lock_group)
{
	int err;

	TRACE(("release_raw_sem(): entry\n"));

#if (COSMOE_SEM_STYLE == POSIX_SEMAPHORES)
	err = sem_post(sem);
#endif

#if (COSMOE_SEM_STYLE == SYSV_SEMAPHORES)
	int group = lock_group ? sem_lock_groups[*sem / SEMMSL] : sem_groups[*sem / SEMMSL];
	int member = *sem % SEMMSL;
	int before;
	int after;
	TRACE(("release_raw_sem(): sem is member %d of group %d\n", member, group));
	struct sembuf sem_lock = {member, 1, SEM_UNDO};
	before = semctl(group, member, GETVAL);
	TRACE(("release_raw_sem(): before semop = %d\n", before));
	err = semop(group, &sem_lock, 1);
	after = semctl(group, member, GETVAL);
	TRACE(("release_raw_sem(): after semop = %d\n", after));

	if (before >= after)
	{
		TRACE(("release_raw_sem(): PANIC! sem not really released!\n"));
	}
#endif

	TRACE(("release_raw_sem(): err = %d, errno = %d (%s)\n", err, errno, strerror(errno)));

	return err;
}


int delete_raw_sem(SEM_TYPE* sem, bool lock_group)
{
	int err;

#if (COSMOE_SEM_STYLE == POSIX_SEMAPHORES)
	err = sem_destroy(sem);
	while (err < 0 && errno == EBUSY)
	{
		TRACE(("delete_raw_sem(): a thread was waiting!\n"));
		sem_post(sem);
		err = sem_destroy(sem);
	}
#endif

#if (COSMOE_SEM_STYLE == SYSV_SEMAPHORES)
	err = release_raw_sem(sem, true); /* make sure nobody's waiting on us */

	if (!lock_group && !err)
		err = release_raw_sem(sem, false); /* make sure nobody's waiting on us */
#endif

	TRACE(("delete_raw_sem(): err = %d, errno = %d\n", err, errno));

	return err;
}


int create_sem_table(size_t size, void** memory, int* memory_id)
{
	int created = 1;

	/* create a unique key for our system-wide sem table */
	key_t table_key = ftok("/usr/local/bin/appserver", (int)'S');

	TRACE(("Master sem table key is 0x%x.\n", table_key));

	/* create and initialize a new semaphore table in shared memory */
	*memory_id = shmget(table_key, size, IPC_CREAT | IPC_EXCL | 0700);
	if (*memory_id == -1 && errno == EEXIST)
	{
		/* grab the existing shared memory semaphore table */
		*memory_id = shmget(table_key, size, IPC_CREAT | 0700);
		TRACE(("Using existing system sem table.\n"));
		created = 0;
	}

	if (*memory_id < 0)
	{
		TRACE(("FATAL: Couldn't setup system sem table: %s\n", strerror(errno)));
		return -1;
	}

	/* point our local table at the master table */
	*memory = shmat(*memory_id, NULL, 0);
	if (*memory == (void *) -1)
	{
		TRACE(("FATAL: Couldn't access sem table: %s\n", strerror(errno)));
		return -1;
	}

#if (COSMOE_SEM_STYLE == SYSV_SEMAPHORES)
	int32 flags = IPC_CREAT | 0700;
	if (created == 1)
		flags = IPC_CREAT | IPC_EXCL | 0700;

	int i;
	key_t key, lockkey;

	for (i = 0; i < num_sem_groups; i++)
	{
		key = ftok("/usr/local/bin/appserver", i);
		lockkey = ftok("/usr/local/bin/appserver", i + num_sem_groups);
		/* create and initialize a new semaphore group */
		sem_groups[i] = semget(key, SEMMSL, flags);
		sem_lock_groups[i] = semget(lockkey, SEMMSL, flags);
		if ((sem_groups[i] == -1) || (sem_lock_groups[i] == -1))
		{
			TRACE(("create_sem_table: semget() failed (%s)\n", strerror(errno)));
		}
	}
#endif

	TRACE(("create_sem_table: returning %d\n", created));
	return created;
}


void construct_sem_timeout(struct timespec* ts, uint32 flags, bigtime_t timeout)
{
#if (COSMOE_SEM_STYLE == POSIX_SEMAPHORES)
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
#endif

#if (COSMOE_SEM_STYLE == SYSV_SEMAPHORES)

	/* SysV wants a relative timeout value */
	if (flags & B_ABSOLUTE_TIMEOUT)
	{
		/* We need to turn this absolute time into a relative one */
		struct timeval now;
		gettimeofday(&now, NULL);
		int64 total_nsec = (timeout * 1000LL) - (now.tv_sec * 1000000000LL) - (now.tv_usec * 1000LL);
		ts->tv_sec = total_nsec / 1000000000LL;
		ts->tv_nsec = total_nsec % 1000000000LL;
	}
	else /* B_RELATIVE_TIMEOUT */
	{
		/* We already have what we need, just convert it */
		ts->tv_sec = timeout / 1000000LL;
		ts->tv_nsec = (timeout % 1000000LL) * 1000L;
	}
#endif

	while (ts->tv_nsec >= 1000000000L)
	{
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000L;
	}
}


void grab_internal_sem_lock(SEM_TYPE* sem, sem_id id)
{
	int err;

	TRACE(("grab_internal_sem_lock: >>> internal lock %s sem %ld\n", (id==2)?"master":"", (long)id));

	do {
		err = acquire_raw_sem(sem, 0, 0, true);
	} while(err==B_INTERRUPTED);

	if (err != 0)
	{
		TRACE(("grab_internal_sem_lock: PANIC - could not obtain internal sem lock"));
	}

	TRACE(("grab_internal_sem_lock: <<< internal lock %s sem %ld\n", (id==2)?"master":"", (long)id));
}

void release_internal_sem_lock(SEM_TYPE* sem, sem_id id)
{
	int err;

	TRACE(("release_internal_sem_lock: >>> internal unlock %s sem %ld\n", (id==2)?"master":"", (long)id));
	err = release_raw_sem(sem, true);
	if (err != 0)
	{
		TRACE(("release_internal_sem_lock: PANIC - could not release internal sem lock"));
	}
	TRACE(("release_internal_sem_lock: <<< internal unlock %s sem %ld\n", (id==2)?"master":"", (long)id));
}

