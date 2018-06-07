/* Semaphore code. Lots of "todo" items */

/*
** Copyright 2004, Bill Hayden. All rights reserved.
** Copyright 2002-2004, The OpenBeOS Team. All rights reserved.
** Distributed under the terms of the OpenBeOS License.
**
** Copyright 2001, Travis Geiselbrecht. All rights reserved.
** Distributed under the terms of the NewOS License.
*/

#include <OS.h>

#include <stdio.h>
#include <errno.h>
#include <sem_internals.h>

#include <string.h>
#include <stdlib.h>

#define TRACE_SEM 0
#if TRACE_SEM
#	define TRACE(x) dprintf x
#	define TRACE_BLOCK(x) dprintf x
#else
#	define TRACE(x) ;
#	define TRACE_BLOCK(x) ;
#endif

#define dprintf printf

struct sem_entry {
	sem_id		id;
	SEM_TYPE	lock;	// protects only the id field when unused
	union {
		// when slot in use
		struct {
			int					count;
			SEM_TYPE			sem;
			thread_id			latest_holder;
			char				name[B_OS_NAME_LENGTH];
			team_id				owner;	// if set to -1, means owned by a port
		} used;

		// when slot unused
		struct {
			sem_id				next_id;
			int					next;
		} unused;
	} u;
};

// Todo: Compute based on the amount of available memory.
#if (COSMOE_SEM_STYLE == SYSV_SEMAPHORES)
static int32 sMaxSems = (SEMMSL * 16);
#else
static int32 sMaxSems = 4096;
#endif
static int32 sUsedSems = 0;

static struct sem_entry *gSems = NULL;
static int         gSemRegion = 0;
static bool              gSemsActive = false;
static struct sem_entry	*gFreeSemsHead = NULL;
static struct sem_entry	*gFreeSemsTail = NULL;

static status_t init_sem(void);
static void teardown_sems(void);
static int sem_delete_owned_sems(team_id owner);

// hidden API
static int dump_sem_list(void);
static void dump_sem(struct sem_entry *sem, int i);



/* gSems[0] is the free list head */
/* gSems[1] is the free list tail */
/* gSems[2] is the sem list lock  */
#define GRAB_SEM_LIST_LOCK()     grab_internal_sem_lock(&gSems[2].lock, 2)
#define RELEASE_SEM_LIST_LOCK()  release_internal_sem_lock(&gSems[2].lock, 2);
#define GRAB_SEM_LOCK(s)         grab_internal_sem_lock(&(s).lock, (s).id)
#define RELEASE_SEM_LOCK(s)      release_internal_sem_lock(&(s).lock, (s).id);



static int
dump_sem_list(void)
{
	int i;

	for (i = 3; i < sMaxSems; i++) {
		if (gSems[i].id >= 0)
			dprintf("%p\tid: %ld\t\tname: '%s'\n", &gSems[i], gSems[i].id,
					gSems[i].u.used.name);
	}
	return 0;
}


static void
dump_sem(struct sem_entry *sem, int i)
{
	dprintf("SEM:     %p\n", sem);
	dprintf("id:      %ld\n", sem->id);
	dprintf("index:   %d\n", i);
	if (sem->id >= 0) {
		dprintf("name:    '%s'\n", sem->u.used.name);
		dprintf("owner:   %ld\n", sem->u.used.owner);
		dprintf("count:   %d\n", sem->u.used.count);
	} else {
		dprintf("next:    %d\n", sem->u.unused.next);
		dprintf("next_id: %ld\n", sem->u.unused.next_id);
	}
}


int
dump_sem_info(int argc, char **argv)
{
	int i;

	if (argc < 2) {
		dump_sem_list();
		return 0;
	}

	// walk through the sems list, trying to match sem number
	for (i = 0; i < sMaxSems; i++) {
		if (gSems[i].id == atol(argv[1])) {
			dump_sem(&gSems[i], i);
			return 0;
		}
	}
	
	dprintf("There is no Cosmoe semaphore with that ID.\n");

	return 0;
}

/*!	\brief Appends a semaphore slot to the free list.

	The semaphore list must be locked.
	The slot's id field is not changed. It should already be set to -1.

	\param slot The index of the semaphore slot.
	\param nextID The ID the slot will get when reused. If < 0 the \a slot
		   is used.
*/
static
void
free_sem_slot(int slot, sem_id nextID)
{
	struct sem_entry *sem = gSems + slot;
	// set next_id to the next possible value; for sanity check the current ID
	if (nextID < 0)
		sem->u.unused.next_id = slot;
	else
		sem->u.unused.next_id = nextID;
	// append the entry to the list
	if (gFreeSemsTail->u.unused.next != -1)
		gSems[gFreeSemsTail->u.unused.next].u.unused.next = slot; // was -1
	else
		gFreeSemsHead->u.unused.next = slot;

	gFreeSemsTail->u.unused.next = slot;
	sem->u.unused.next = -1; // This is now the end of the queue
}

status_t
init_sem(void)
{
	int i;

	if (gSems)
		return 0;

	TRACE(("sem_init: entry\n"));

	// create and initialize semaphore table
	i = create_sem_table(sizeof(struct sem_entry)*sMaxSems, (void**)&gSems, &gSemRegion);

	gFreeSemsHead = &gSems[0];
	gFreeSemsTail = &gSems[1];

	if (i == 1)
	{
		/* This process is creating the table */
		memset(gSems, 0, sizeof(struct sem_entry) * sMaxSems);

		gFreeSemsTail->u.unused.next = -1;
		gFreeSemsHead->u.unused.next = -1;
		create_raw_sem(&gSems[0].lock, 0, true, true);	// sem queue head lock
		create_raw_sem(&gSems[1].lock, 1, true, true);	// sem queue tail lock
		create_raw_sem(&gSems[2].lock, 2, true, true);	// master sem table lock

		for (i = 3; i < sMaxSems; i++) {
			create_raw_sem(&gSems[i].lock, i, true, true);
			gSems[i].id = -1;
			free_sem_slot(i, i);
		}

		TRACE(("sem_init: sem table created from scratch\n"));
	}
	else if (i == 0)
	{
		TRACE(("sem_init: existing sem table found\n"));
	}
	else if (i == -1)
	{
		TRACE(("sem_init: PANIC: sem table could not be created\n"));
		return B_ERROR;
	}

	atexit(teardown_sems);
	
	TRACE(("sem_init: exit\n"));

	gSemsActive = true;

	return 0;
}


/**	Creates a semaphore with the given parameters.
 *	Note, the team_id is not checked, it must be correct, or else
 *	that semaphore might not be deleted.
 *	This function is not available from within OS.h, and
 *	should not be made public - if possible, we should remove it
 *	completely (and have only create_sem() exported).
 */
sem_id
create_sem_etc(int32 count, const char *name, team_id owner)
{
	struct sem_entry *sem = NULL;
	sem_id retval = B_NO_MORE_SEMS;
	int name_len;

	TRACE(("create_sem_etc(count = %ld, name = %s): entry\n", count, name));

	if (gSemsActive == false)
		init_sem();
	if (gSemsActive == false)
		return B_NO_MORE_SEMS;

	if (name == NULL)
		name = "unnamed semaphore";

	name_len = strlen(name) + 1;
	name_len = min(name_len, B_OS_NAME_LENGTH);

	GRAB_SEM_LIST_LOCK();

	// get the first slot from the free list
	if (gFreeSemsHead->u.unused.next != -1)
		sem = &gSems[gFreeSemsHead->u.unused.next];
	else
		TRACE(("create_sem_etc(%s): panic - no free sem head\n", name));

	if (sem) {
		// remove it from the free list
		gFreeSemsHead->u.unused.next = sem->u.unused.next;
		// init the slot
		GRAB_SEM_LOCK(*sem);
		sem->id = sem->u.unused.next_id;
		sem->u.used.count = count;
		strncpy(sem->u.used.name, name, name_len);
		sem->u.used.owner = owner;
		create_raw_sem(&sem->u.used.sem, sem->id, false, false);
		retval = sem->id;
		RELEASE_SEM_LOCK(*sem);

		atomic_add(&sUsedSems, 1);
	}

	RELEASE_SEM_LIST_LOCK();

	TRACE(("create_sem_etc(%s): returning %ld\n", name, retval));

	return retval;
}

sem_id
create_sem(int32 count, const char *name)
{
	return create_sem_etc(count, name, getpid());
}

status_t
delete_sem(sem_id id)
{
	return delete_sem_etc(id, 0, false);
}


status_t
delete_sem_etc(sem_id id, status_t return_code, bool interrupted)
{
	int slot;

	TRACE(("delete_sem_etc(%ld): entry\n", id));

	if (gSemsActive == false)
		init_sem();
	if (gSemsActive == false)
		return B_NO_MORE_SEMS;
	if (id < 0)
		return B_BAD_SEM_ID;

	slot = id % sMaxSems;

	GRAB_SEM_LOCK(gSems[slot]);

	if (gSems[slot].id != id) {
		RELEASE_SEM_LOCK(gSems[slot]);
		TRACE(("delete_sem: invalid sem_id %ld\n", id));
		return B_BAD_SEM_ID;
	}

	// free any threads waiting for this semaphore
	delete_raw_sem(&gSems[slot].u.used.sem, false);

	gSems[slot].id = -1;

	RELEASE_SEM_LOCK(gSems[slot]);

	// append slot to the free list
	GRAB_SEM_LIST_LOCK();
	free_sem_slot(slot, id + sMaxSems);
	atomic_add(&sUsedSems, -1);
	RELEASE_SEM_LIST_LOCK();

	return B_OK;
}


status_t
acquire_sem(sem_id id)
{
	return acquire_sem_etc(id, 1, 0, 0);
}


status_t
acquire_sem_etc(sem_id id, int32 count, uint32 flags, bigtime_t timeout)
{
	int slot = id % sMaxSems;
	status_t status = B_OK;

	TRACE(("acquire_sem_etc(%ld): entry\n", id));

	if (gSemsActive == false)
		init_sem();
	if (gSemsActive == false)
		return B_NO_MORE_SEMS;

	if (id < 0)
		return B_BAD_SEM_ID;
	if (count <= 0)
		return B_BAD_VALUE;
	if (count > 1)
	{
		TRACE(("acquire_sem_etc(%ld): PANIC: Cosmoe only support count of 1\n", id));
	}

	GRAB_SEM_LOCK(gSems[slot]);

	if (gSems[slot].id != id) {
		TRACE(("acquire_sem_etc: bad sem_id %ld\n", id));
		status = B_BAD_SEM_ID;
		goto err;
	}

	if (gSems[slot].u.used.count - count < 0 && (flags & B_TIMEOUT) != 0
		&& timeout <= 0) {
		// immediate timeout
		status = B_WOULD_BLOCK;
		goto err;
	}

	TRACE(("acquire_sem_etc(%ld): decrementing cached count by %ld\n", id, count));

	if ((gSems[slot].u.used.count -= count) < 0) {
		// we need to block
		SEM_TYPE* saved_sem;
		
		TRACE_BLOCK(("acquire_sem_etc(id = %ld): block name = %s\n", id, gSems[slot].u.used.name));

		saved_sem = &gSems[slot].u.used.sem;
		RELEASE_SEM_LOCK(gSems[slot]);
		
		// sem lock must be unlocked, or we won't be able to release the sem later
		status = acquire_raw_sem(saved_sem, flags, timeout, false);
		
		GRAB_SEM_LOCK(gSems[slot]);
		
		if (status == B_OK)
			gSems[slot].u.used.latest_holder = find_thread(NULL);
		else
		{
			/* We failed to obtain the lock, so undo our sem count adjustment */
			TRACE(("acquire_sem_etc(%ld): lock not obtained! (%ld)\n", (long)id, status));
			gSems[slot].u.used.count += count;	/* lock attempt failed */
		}

		TRACE_BLOCK(("acquire_sem_etc(%ld): exit block\n", id));
	}

err:
	RELEASE_SEM_LOCK(gSems[slot]);

	return status;
}

status_t
release_sem(sem_id id)
{
	return release_sem_etc(id, 1, 0);
}


status_t
release_sem_etc(sem_id id, int32 count, uint32 flags)
{
	int slot = id % sMaxSems;
	status_t status = B_OK;

	TRACE(("release_sem_etc(%ld): enter\n", id));

	if (gSemsActive == false)
		init_sem();
	if (gSemsActive == false)
		return B_NO_MORE_SEMS;
	if (id < 0)
		return B_BAD_SEM_ID;
	if (count <= 0)
		return B_BAD_VALUE;

	GRAB_SEM_LOCK(gSems[slot]);

	if (gSems[slot].id != id) {
		TRACE(("sem_release_etc: invalid sem_id %ld\n", id));
		status = B_BAD_SEM_ID;
		goto err;
	}

	while (count > 0) {
		int delta = 1;
		if (gSems[slot].u.used.count < 0) {
			release_raw_sem(&gSems[slot].u.used.sem, false);
		}
		gSems[slot].u.used.count += delta;
		count -= delta;
	}

err:
	RELEASE_SEM_LOCK(gSems[slot]);

	return status;
}


status_t
get_sem_count(sem_id id, int32 *thread_count)
{
	int slot;

	TRACE(("get_sem_count(%ld): enter\n", id));

	if (gSemsActive == false)
		init_sem();
	if (gSemsActive == false)
		return B_NO_MORE_SEMS;
	if (id < 0)
		return B_BAD_SEM_ID;
	if (thread_count == NULL)
		return EINVAL;

	slot = id % sMaxSems;

	GRAB_SEM_LOCK(gSems[slot]);

	if (gSems[slot].id != id) {
		RELEASE_SEM_LOCK(gSems[slot]);
		TRACE(("sem_get_count: invalid sem_id %ld\n", id));
		return B_BAD_SEM_ID;
	}

	*thread_count = gSems[slot].u.used.count;

	RELEASE_SEM_LOCK(gSems[slot]);

	return B_NO_ERROR;
}


/** Fills the thread_info structure with information from the specified
 *	thread.
 *	The thread lock must be held when called.
 */

static void
fill_sem_info(struct sem_entry *sem, sem_info *info, size_t size)
{
	info->sem = sem->id;
	info->team = sem->u.used.owner;
	strncpy(info->name, sem->u.used.name, sizeof(info->name));
	info->count = sem->u.used.count;
	info->latest_holder	= sem->u.used.latest_holder;
}


/** The underscore is needed for binary compatibility with BeOS.
 *	OS.h contains the following macro:
 *	#define get_sem_info(sem, info)                \
 *            _get_sem_info((sem), (info), sizeof(*(info)))
 */

status_t
_get_sem_info(sem_id id, struct sem_info *info, size_t size)
{
	status_t status = B_OK;
	int slot;

	TRACE(("_get_sem_info(%ld): enter\n", id));

	if (!gSemsActive)
		init_sem();
	if (!gSemsActive)
		return B_NO_MORE_SEMS;
	if (id < 0)
		return B_BAD_SEM_ID;
	if (info == NULL || size != sizeof(sem_info))
		return B_BAD_VALUE;

	slot = id % sMaxSems;

	GRAB_SEM_LOCK(gSems[slot]);

	if (gSems[slot].id != id) {
		status = B_BAD_SEM_ID;
		TRACE(("get_sem_info: invalid sem_id %ld\n", id));
	} else
		fill_sem_info(&gSems[slot], info, size);

	RELEASE_SEM_LOCK(gSems[slot]);

	return status;
}

/** The underscore is needed for binary compatibility with BeOS.
 *	OS.h contains the following macro:
 *	#define get_next_sem_info(team, cookie, info)  \
 *           _get_next_sem_info((team), (cookie), (info), sizeof(*(info)))
 */
status_t
_get_next_sem_info(team_id team, int32 *_cookie, struct sem_info *info, size_t size)
{
	int slot;
	bool found = false;

	TRACE(("_get_next_sem_info(team = %ld): enter\n", team));

	if (!gSemsActive)
		init_sem();
	if (!gSemsActive)
		return B_NO_MORE_SEMS;
	if (_cookie == NULL || info == NULL || size != sizeof(sem_info))
		return B_BAD_VALUE;

	if (team == B_CURRENT_TEAM)
		team = getpid();
	/* prevents gSems[].owner == -1 >= means owned by a port */
	if (team < 0)
		return B_BAD_TEAM_ID; 

	slot = *_cookie;
	if (slot >= sMaxSems)
		return B_BAD_VALUE;

	GRAB_SEM_LIST_LOCK();

	while (slot < sMaxSems) {
		if (gSems[slot].id != -1 && gSems[slot].u.used.owner == team) {
			GRAB_SEM_LOCK(gSems[slot]);
			if (gSems[slot].id != -1 && gSems[slot].u.used.owner == team) {
				// found one!
				fill_sem_info(&gSems[slot], info, size);

				RELEASE_SEM_LOCK(gSems[slot]);
				slot++;
				found = true;
				break;
			}
			RELEASE_SEM_LOCK(gSems[slot]);
		}
		slot++;
	}
	RELEASE_SEM_LIST_LOCK();

	if (!found)
		return B_BAD_VALUE;

	*_cookie = slot;
	return B_OK;
}

status_t
set_sem_owner(sem_id id, team_id team)
{
	int slot;

	TRACE(("set_sem_owner(%ld): enter\n", id));

	if (gSemsActive == false)
		init_sem();
	if (gSemsActive == false)
		return B_NO_MORE_SEMS;
	if (id < 0)
		return B_BAD_SEM_ID;
	if (team < 0)
		return B_BAD_TEAM_ID;

	slot = id % sMaxSems;

	GRAB_SEM_LOCK(gSems[slot]);

	if (gSems[slot].id != id) {
		RELEASE_SEM_LOCK(gSems[slot]);
		TRACE(("set_sem_owner: invalid sem_id %ld\n", id));
		return B_BAD_SEM_ID;
	}

	// ToDo: this is a small race condition: the team ID could already
	// be invalid at this point - we would lose one semaphore slot in
	// this case!
	// The only safe way to do this is to prevent either team (the new
	// or the old owner) from dying until we leave the spinlock.
	gSems[slot].u.used.owner = team;

	RELEASE_SEM_LOCK(gSems[slot]);


	return B_NO_ERROR;
}


/** this function cycles through the sem table, deleting all the sems that are owned by
 *	the passed team_id
 */

static int
sem_delete_owned_sems(team_id owner)
{
	int i;
	int count = 0;

	if (owner < 0)
		return B_BAD_TEAM_ID;

	GRAB_SEM_LIST_LOCK();

	for (i = 0; i < sMaxSems; i++) {
		if (gSems[i].id != -1 && gSems[i].u.used.owner == owner) {
			sem_id id = gSems[i].id;

			RELEASE_SEM_LIST_LOCK();

			delete_sem(id);
			count++;

			GRAB_SEM_LIST_LOCK();
		}
	}

	RELEASE_SEM_LIST_LOCK();

	return count;
}


int32
sem_max_sems(void)
{
	return sMaxSems;
}


int32
sem_used_sems(void)
{
	return sUsedSems;
}


//	#pragma mark -


void
teardown_sems(void)
{
	if (!gSemsActive)
	{
		printf("teardown_sems(): no sems to delete\n");
		return;
	}

	/* remove all sems owned by our team */
	int num_deleted = sem_delete_owned_sems(getpid());

	printf("teardown_sems(): %d sems deleted\n", num_deleted);
}



