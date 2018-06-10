/* Threading routines */

/*
** Copyright 2004, Bill Hayden. All rights reserved.
** Copyright 2002-2004, The OpenBeOS Team. All rights reserved.
** Distributed under the terms of the OpenBeOS License.
**
** Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
** Distributed under the terms of the NewOS License.
*/

#include <OS.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TRACE_THREAD 1
#ifdef TRACE_THREAD
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif

/* FIXME: Threads that die normally do not remove their own entries from */
/*        the thread table.  They remain forever...                      */

/* Todo: Compute based on the amount of available memory. */
#define MAX_THREADS 4096

#define FREE_SLOT 0xFFFFFFFF

typedef void* (*pthread_entry) (void*);

thread_info *thread_table = NULL;
static int thread_shm = -1;

static void init_thread(void);
static void teardown_threads(void);

/* TODO: table access is not protected by a semaphore */

static void
init_thread(void)
{
	key_t table_key;
	bool created = true;
	int size = sizeof(thread_info) * MAX_THREADS;

	if (thread_table)
		return;

	/* grab a (hopefully) unique key for our table */
	table_key = ftok("/usr/local/bin/", (int)'T');

	/* create and initialize a new semaphore table in shared memory */
	thread_shm = shmget(table_key, size, IPC_CREAT | IPC_EXCL | 0700);
	if (thread_shm == -1 && errno == EEXIST)
	{
		/* grab the existing shared memory thread table */
		thread_shm = shmget(table_key, size, IPC_CREAT | 0700);
		created = false;
	}

	if (thread_shm < 0)
	{
		printf("FATAL: Couldn't setup thread table: %s\n", strerror(errno));
		return;
	}

	/* point our local table at the master table */
	thread_table = shmat(thread_shm, NULL, 0);
	if (thread_table == (void *) -1)
	{
		printf("FATAL: Couldn't load thread table: %s\n", strerror(errno));
		return;
	}

	if (created)
	{
		int i;
		/* POTENTIAL RACE: table exists but is uninitialized until here */
		for (i = 0; i < MAX_THREADS; i++)
			thread_table[i].thread = FREE_SLOT;

	}

	atexit(teardown_threads);
}


thread_id
spawn_thread(thread_func func, const char *name, int32 priority, void *data)
{
	init_thread();

	int i;
	for (i = 0; i < MAX_THREADS; i++)
	{
		if (thread_table[i].thread == FREE_SLOT)
		{
			if (!name)
				name = "no-name thread";

			thread_table[i].pth = -1; //not in the POSIX system yet
			thread_table[i].thread = i;
			thread_table[i].team = getpid();
			thread_table[i].priority = priority;
			thread_table[i].state = B_THREAD_SPAWNED;
			strncpy(thread_table[i].name, name, B_OS_NAME_LENGTH);
			thread_table[i].name[B_OS_NAME_LENGTH - 1] = '\0';
			thread_table[i].func = func;
			thread_table[i].data = data;
			thread_table[i].code = 0;
			thread_table[i].sender = 0;
			thread_table[i].buffer = NULL;

			return i;
		}
	}

	return B_ERROR;
}


status_t
kill_thread(thread_id thread)
{
	init_thread();

	int i;
	for (i = 0; i < MAX_THREADS; i++)
	{
		if (thread_table[i].thread == thread)
		{
			if (pthread_kill(thread_table[i].pth, SIGKILL) == 0)
			{
				thread_table[i].thread = FREE_SLOT;
				thread_table[i].team = 0;
				if (thread_table[i].buffer)
					free(thread_table[i].buffer);

				return B_OK;
			}
			break;
		}
	}

	return B_BAD_THREAD_ID;
}


status_t
rename_thread(thread_id thread, const char *newName)
{
	init_thread ();

	int i;
	for (i = 0; i < MAX_THREADS; i++)
	{
		if (thread_table[i].thread == thread)
		{
			strncpy(thread_table[i].name, newName, B_OS_NAME_LENGTH);
			thread_table[i].name[B_OS_NAME_LENGTH - 1] = '\0';

			return B_OK;
		}
	}

	return B_BAD_THREAD_ID;
}


void
exit_thread(status_t status)
{
	pthread_t this_thread = pthread_self();
	int i;
	
	init_thread();
	
	for (i = 0; i < MAX_THREADS; i++)
	{
		if (thread_table[i].pth == this_thread)
		{
			thread_table[i].thread = FREE_SLOT;
			thread_table[i].team = 0;
			if (thread_table[i].buffer)
				free(thread_table[i].buffer);
			break;
		}
	}
	
	pthread_exit((void *) &status);
}


status_t
on_exit_thread(void (*callback)(void *), void *data)
{
    return B_NO_MEMORY;
}


status_t
send_data(thread_id thread, int32 code, const void *buffer, size_t buffer_size)
{
	init_thread();

	int i;
	for (i = 0; i < MAX_THREADS; i++)
	{
		if (thread_table[i].thread == thread)
		{
			thread_table[i].code = code;
			if (buffer)
			{
				if (!thread_table[i].buffer)
					thread_table[i].buffer = malloc(buffer_size);
				memcpy(thread_table[i].buffer, buffer, buffer_size);
			}

			return B_OK;
		}
	}

	return B_BAD_THREAD_ID;
}


status_t
receive_data(thread_id *sender, void *buffer, size_t bufferSize)
{
	init_thread();

	int i;
	for (i = 0; i < MAX_THREADS; i++)
	{
		if (thread_table[i].thread != FREE_SLOT)
		{
			if (*sender)
				thread_table[i].sender = *sender;

			while (!thread_table[i].buffer)
				continue;

			if (thread_table[i].buffer)
			{
				memcpy(buffer, thread_table[i].buffer, bufferSize);
				free(thread_table[i].buffer);
			}

			return B_OK;
		}
	}

	return B_BAD_THREAD_ID;
}


bool
has_data(thread_id thread)
{
	init_thread();

	int i;
	for (i = 0; i < MAX_THREADS; i++)
	{
		if (thread_table[i].thread == thread)
			return (thread_table[i].buffer != NULL);
	}

	return false;
}


void teardown_threads()
{
	int count = 0;
	int i;
	
	/* Free thread table entries created by our process */
	for (i = 0; i < MAX_THREADS; i++)
	{
		if (thread_table[i].team == getpid())
		{
			thread_table[i].thread = FREE_SLOT;
			thread_table[i].team = 0;
			count++;
		}
	}
	
	printf("teardown_threads(): %d threads deleted\n", count);
}


status_t
_get_thread_info(thread_id id, thread_info *info, size_t size)
{
	init_thread();

	if (info == NULL || size != sizeof(thread_info) || id < B_OK)
		return B_BAD_VALUE;

	int i;
	for (i = 0; i < MAX_THREADS; i++)
	{
		if (thread_table[i].thread == id)
		{
			info->thread = id;
			strncpy (info->name, thread_table[i].name, B_OS_NAME_LENGTH);
			info->name[B_OS_NAME_LENGTH - 1] = '\0';
			info->state = thread_table[i].state;
			info->priority = thread_table[i].priority;
			info->team = thread_table[i].team;
			return B_OK;
		}
	}

	return B_BAD_VALUE;
}


status_t
_get_next_thread_info(team_id team, int32 *_cookie, thread_info *info, size_t size)
{
	init_thread();

	if (info == NULL || size != sizeof(thread_info) || *_cookie < 0)
		return B_BAD_VALUE;
	
	int i;
	for (i = *_cookie; i < MAX_THREADS; i++)
	{
		if (thread_table[i].team == team)
		{
			*_cookie = i + 1;

			return _get_thread_info(thread_table[i].thread, info, size);
		}
	}

	return B_BAD_VALUE;
}


thread_id
find_thread(const char *name)
{
	init_thread();

	pthread_t pth = 0;

	if (name == NULL)
		pth = pthread_self();

	int i;
	for (i = 0; i < MAX_THREADS; i++)
	{
		if (thread_table[i].thread != FREE_SLOT)
		{
			if (!pth)
			{
				if (strcmp(thread_table[i].name, name) == 0)
					return i;
			}
			else
			{
				if (thread_table[i].pth == pth)
					return i;
			}
		}
	}

	return B_NAME_NOT_FOUND;
}


status_t
set_thread_priority(thread_id id, int32 priority)
{
	init_thread();

	int i;
	for (i = 0; i < MAX_THREADS; i++)
	{
		if (thread_table[i].thread == id)
		{
			thread_table[i].priority = priority;
			return B_OK;
		}
	}

	return B_BAD_THREAD_ID;
}


status_t
snooze(bigtime_t timeout)
{
	int err;
	
	err = usleep((unsigned long)timeout);

	if (err < 0 && errno == EINTR)
		return B_INTERRUPTED;

	return B_OK;
}


status_t
snooze_etc(bigtime_t amount, int timeBase, uint32 flags)
{
	// TODO: determine what timeBase and flags do
	return snooze(amount);
}


status_t
wait_for_thread(thread_id id, status_t *_returnCode)
{
	if (_returnCode == NULL)
		return B_BAD_VALUE;

	init_thread();

	int i;
	for (i = 0; i < MAX_THREADS; i++)
	{
		if (thread_table[i].thread == id)
		{
			if (pthread_join(thread_table[i].pth, (void**)_returnCode) == 0)
				return B_OK;
			break;
		}
	}

	return B_BAD_THREAD_ID;
}


status_t
suspend_thread(thread_id id)
{
	init_thread();

	int i;
	for (i = 0; i < MAX_THREADS; i++)
	{
		if (thread_table[i].thread == id)
		{
			pthread_kill(thread_table[i].pth, SIGSTOP);
			thread_table[i].state = B_THREAD_SUSPENDED;

			return B_OK;
		}
	}

	return B_BAD_THREAD_ID;
}


status_t
resume_thread(thread_id id)
{
	init_thread();

	int i;
	for (i = 0; i < MAX_THREADS; i++)
	{
		if (thread_table[i].thread == id)
		{
			switch (thread_table[i].state)
			{
				case B_THREAD_SPAWNED:
				{
					pthread_t tid;

					if (pthread_create(&tid, NULL, (pthread_entry)thread_table[i].func,
										thread_table[i].data) == 0)
					{
						thread_table[i].pth = tid;
						thread_table[i].state = B_THREAD_RUNNING;
						return B_OK;
					}

					return B_ERROR;
				}

				case B_THREAD_SUSPENDED:
					pthread_kill(thread_table[i].pth, SIGCONT);
					thread_table[i].state = B_THREAD_RUNNING;
					return B_OK;

				default:
					return B_BAD_THREAD_STATE;
			}
		}
	}

	return B_BAD_THREAD_ID;
}


status_t kill_team(team_id team)
{
	status_t ret = B_OK;
	int err;
	 
	err = kill((pid_t)team, SIGKILL);
	if (err < 0 && errno == ESRCH)
		ret = B_BAD_TEAM_ID;

	return ret;
}


status_t _get_team_info(team_id id, team_info *info, size_t size)
{
	if (size != sizeof(team_info))
		return B_ERROR;
		
	if (info == NULL)
		return B_ERROR;
		
	info->team = id;
	
	return B_OK;
}
