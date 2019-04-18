/* ports for IPC */

/*
** Copyright 2018-2019, Dario Casalinuovo. All rights reserved.
** Copyright 2004, Bill Hayden. All rights reserved.
** Copyright 2002-2004, The OpenBeOS Team. All rights reserved.
** Distributed under the terms of the OpenBeOS License.
**
** Copyright 2001, Mark-Jan Bastian. All rights reserved.
** Distributed under the terms of the NewOS License.
*/

#include <OS.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>

#include <string.h>
#include <stdlib.h>

#define dprintf printf
#define panic printf
#define team_get_current_team_id getpid

//#define TRACE_PORTS
#ifdef TRACE_PORTS
#	define TRACE(x) dprintf x
#else
#	define TRACE(x)
#endif

#ifndef DEBUG
#define DEBUG 1
#endif

#define PORT_MAX_MESSAGE_SIZE 4096

typedef struct port_msg {
	int32		code;
	char		buffer_chain[PORT_MAX_MESSAGE_SIZE];
	size_t		size;
} port_msg;

struct port_entry {
	port_id 	id;
	team_id 	owner;
	int32 		capacity;
	sem_id		lock;
	char		name[B_OS_NAME_LENGTH];
	sem_id		read_sem;
	sem_id		write_sem;
	int32		total_count;
	int			queue_shm;
	int32		head;
	int32		tail;
};

// hidden API
static int dump_port_list(void);
static void _dump_port_info(struct port_entry *port);

// gMaxPorts must be power of 2
int32 gMaxPorts = 256;

#define MAX_QUEUE_LENGTH 256

/* This implementation is a major memory hog, so let's not
** make things worse than they already are! A 64k message? */
/* #define PORT_MAX_MESSAGE_SIZE 65536 */

static int sPortArea = -1;
static void *sPortMemory = NULL;
static sem_id sPortSem = -1;

static struct port_entry *sPorts = NULL;
static port_id* sNextPort = NULL;

static bool sPortsActive = false;

#define GRAB_PORT_LIST_LOCK() do {} while(acquire_sem(sPortSem) == B_INTERRUPTED)
#define RELEASE_PORT_LIST_LOCK() release_sem(sPortSem);
#define GRAB_PORT_LOCK(s) if ((s).lock != -1) do {} while(acquire_sem((s).lock) == B_INTERRUPTED)
#define RELEASE_PORT_LOCK(s) if ((s).lock != -1) release_sem((s).lock)

static status_t port_init(void);
static void teardown_ports(void);
static int delete_owned_ports(team_id owner);


status_t
port_init(void)
{
	int size = sizeof(sem_id) + sizeof(port_id) + (sizeof(struct port_entry) * gMaxPorts);
	key_t table_key;
	bool created = true;

	if (sPorts)
		return B_OK;

	/* grab a (hopefully) unique key for our table */
	table_key = ftok("/usr/local/bin/", (int)'P');
	TRACE(("Using key %x for the port table\n", (int)table_key));
	TRACE(("The size of the port table is %ld bytes\n", (long)size));

	// create and initialize ports table in shared memory
	sPortArea = shmget(table_key, size, IPC_CREAT | IPC_EXCL | 0700);
	if (sPortArea == -1 && errno == EEXIST) {
		/* get existing semaphore table in shared memory */
		sPortArea = shmget(table_key, size, IPC_CREAT | 0700);
		TRACE(("Using pre-existing master ports table\n"));
		created = false;
	}

	if (sPortArea < 0) {
		TRACE(("FATAL: Couldn't setup port table due to "
			"error %d (%s)\n", errno, strerror(errno)));
		return B_ERROR;
	}

	/* point our local table at the master table */
	sPortMemory = shmat(sPortArea, NULL, 0);
	if (sPortMemory == (void *) -1) {
		TRACE(("FATAL: Couldn't attach port table: %s\n", strerror (errno)));
		return B_ERROR;
	}

	sNextPort = (port_id *)sPortMemory + sizeof(sem_id);
	sPorts = (void*)sNextPort + sizeof(port_id);

	if (created) {
		memset(sNextPort, 0, size);
		for (uint32 i = 0; i < gMaxPorts; i++) {
			sPorts[i].id = -1;
			sPorts[i].lock = -1;
		}

		sPortSem = create_sem(1, "master port lock");
		*((sem_id *)sPortMemory) = sPortSem;
	} else
		sPortSem = *((sem_id *)sPortMemory);

	atexit(teardown_ports);

	TRACE(("port_init: exit\n"));

	sPortsActive = true;

	return 0;
}



static int
dump_port_list(void)
{
	for (uint32 i = 0; i < gMaxPorts; i++) {
		if (sPorts[i].id >= 0) {
			dprintf("%p\tid: %ld\t\tname: '%s'\n", &sPorts[i],
				sPorts[i].id, sPorts[i].name);
		}
	}
	return 0;
}


static void
_dump_port_info(struct port_entry *port)
{
	int32 cnt;
	dprintf("PORT:      %p\n", port);
	dprintf("name:      '%s'\n", port->name);
	dprintf("owner:     %ld\n", port->owner);
	dprintf("capacity:  %ld\n", port->capacity);
	get_sem_count(port->read_sem, &cnt);
	dprintf("read_sem:  %ld (count %ld)\n", port->read_sem, cnt);
	get_sem_count(port->write_sem, &cnt);
	dprintf("write_sem: %ld (count %ld)\n", port->write_sem, cnt);
}


int
dump_port_info(int argc, char **argv)
{
	int is_number;
	
	port_init();
	 
	if (!sPortsActive) {
		dprintf("No Cosmoe ports in use.\n");
		return 0;
	}

	if (argc < 2) {
		dump_port_list();
		return 0;
	}
	
	is_number = isdigit(argv[1][0]);

	// walk through the ports list, trying to match number or name
	for (uint32 i = 0; i < gMaxPorts; i++) {
		if (is_number) {
			if (sPorts[i].id == atoi(argv[1])) {
				_dump_port_info(&sPorts[i]);
				return 0;
			}
		} else if (sPorts[i].name != NULL
				&& strcmp(argv[1], sPorts[i].name) == 0) {
			_dump_port_info(&sPorts[i]);
			return 0;
		}
	}
	return 0;
}

/** this function cycles through the ports table, deleting all
 *	the ports that are owned by the passed team_id
 */

int
delete_owned_ports(team_id owner)
{
	// ToDo: investigate maintaining a list of ports in the team
	//	to make this simpler and more efficient.
	int count = 0;

	if (!sPortsActive)
		return B_BAD_PORT_ID;

	GRAB_PORT_LIST_LOCK();

	for (uint32 i = 0; i < gMaxPorts; i++) {
		if (sPorts[i].id != -1 && sPorts[i].owner == owner) {
			port_id id = sPorts[i].id;

			RELEASE_PORT_LIST_LOCK();

			delete_port(id);
			count++;

			GRAB_PORT_LIST_LOCK();
		}
	}

	RELEASE_PORT_LIST_LOCK();

	return count;
}


static void
put_port_msg(port_msg *msg)
{
	msg->buffer_chain[0] = '\0';
	msg->code = 0;
	msg->size = 0;
}


port_id		
_kern_create_port(int32 queueLength, const char *name)
{
	sem_id readSem, writeSem, portSem;
	port_id returnValue;
	team_id	owner;

	if (!sPortsActive)
		port_init();
	if (!sPortsActive)
		return B_BAD_PORT_ID;

	// check queue length
	if (queueLength < 1
			|| queueLength > MAX_QUEUE_LENGTH) {
		return B_BAD_VALUE;
	}

	// check & dup name
	if (name == NULL)
		name = "unnamed port";

	// ToDo: we could save the memory and use the semaphore name only instead

	// create read sem with owner set to -1
	// ToDo: should be B_SYSTEM_TEAM
	portSem = create_sem_etc(1, name, -1);
	if (portSem < B_OK) {
		// cleanup
		return portSem;
	}

	// create read sem with owner set to -1
	// ToDo: should be B_SYSTEM_TEAM
	readSem = create_sem_etc(0, name, -1);
	if (readSem < B_OK) {
		// cleanup
		delete_sem(portSem);
		return readSem;
	}

	// create write sem
	writeSem = create_sem_etc(queueLength, name, -1);
	if (writeSem < 0) {
		// cleanup
		delete_sem(readSem);
		delete_sem(portSem);
		return writeSem;
	}

	owner = team_get_current_team_id();

	GRAB_PORT_LIST_LOCK();

	// find the first empty spot
	for (uint32 i = 0; i < gMaxPorts; i++) {
		if (sPorts[i].id == -1) {
			key_t  port_shm_key;
			const size_t size = sizeof(port_msg) * queueLength;
			void* msg_queue;

			// make the port_id be a multiple of the slot it's in
			if (i >= *sNextPort % gMaxPorts)
				*sNextPort += i - *sNextPort % gMaxPorts;
			else
				*sNextPort += gMaxPorts - (*sNextPort % gMaxPorts - i);

			if (sPorts[i].lock == -1)
				sPorts[i].lock = create_sem(1, "port lock");
			GRAB_PORT_LOCK(sPorts[i]);
			sPorts[i].id = (*sNextPort)++;
			RELEASE_PORT_LIST_LOCK();

			strncpy(sPorts[i].name, name, B_OS_NAME_LENGTH);
			sPorts[i].capacity = queueLength;
			sPorts[i].owner = owner;
			sPorts[i].name[B_OS_NAME_LENGTH - 1] = '\0';

			// assign sem
			sPorts[i].read_sem	= readSem;
			sPorts[i].write_sem	= writeSem;
			sPorts[i].lock = portSem;

			sPorts[i].total_count = 0;

			sPorts[i].head		= 0;
			sPorts[i].tail		= 0;

			/* grab a (hopefully) unique key for our port */
			char path[B_PATH_NAME_LENGTH];
			static int static_port_count = 0;
			sprintf(path, "/proc/%d/exe", getpid());
			port_shm_key = ftok(path, ++static_port_count);	/* HACK ALERT */

			TRACE(("create_port: generated port queue key %d from %s + %d.\n", port_shm_key, path, static_port_count));
			/* create and initialize a new semaphore table in shared memory */
			sPorts[i].queue_shm = shmget(port_shm_key, size, IPC_CREAT | IPC_EXCL | 0700);
			if (sPorts[i].queue_shm == -1 && errno == EEXIST) {
				/* TODO: this should be FATAL */
				/* TODO: we don't know if it is large enough */
				sPorts[i].queue_shm = shmget(port_shm_key, size, IPC_CREAT | 0700);
				TRACE(("WARNING: Using pre-existing port queue.\n"));
			}

			if (sPorts[i].queue_shm < 0) {
				TRACE(("FATAL: Couldn't setup port queue with key %d: %s\n",
						port_shm_key,
						strerror(errno)));
				returnValue = B_NO_MEMORY;
				sPorts[i].id = -1;
				goto cleanup;
			}

			TRACE(("Port %d named %s is using shm key %x\n", i, name, port_shm_key));

			/* point our local table at the master table */
			msg_queue = shmat(sPorts[i].queue_shm, NULL, 0);
			if (msg_queue == (void *) -1) {
				printf("Couldn't attach port queue: %s\n", strerror(errno));
				returnValue = B_NO_MEMORY;
				sPorts[i].id = -1;
				goto cleanup;
			}

			TRACE(("Port %d is now attached successfully\n", i));

			port_msg* p = msg_queue;
			for (uint32 j = 0; j < queueLength; j++) {
				p[j].buffer_chain[0] = '\0';
				p[j].code = 0;
				p[j].size = 0;
			}

			shmdt(msg_queue);

			returnValue = sPorts[i].id;

			RELEASE_PORT_LOCK(sPorts[i]);
			goto out;
		}
	}

	// not enough gPorts...
	RELEASE_PORT_LIST_LOCK();
	returnValue = B_NO_MORE_PORTS;
	dprintf("create_port(): B_NO_MORE_PORTS\n");

	// cleanup
cleanup:
	delete_sem(writeSem);
	delete_sem(readSem);
	delete_sem(portSem);

out:

	return returnValue;
}


status_t
_kern_close_port(port_id id)
{
	int slot;

	if (!sPortsActive)
		port_init();
	if (!sPortsActive || id < 0)
		return B_BAD_PORT_ID;

	slot = id % gMaxPorts;

	// walk through the sem list, trying to match name
	GRAB_PORT_LOCK(sPorts[slot]);

	if (sPorts[slot].id != id) {
		RELEASE_PORT_LOCK(sPorts[slot]);
		dprintf("close_port: invalid port_id %ld\n", id);
		return B_BAD_PORT_ID;
	}

	// mark port to disable writing
	sPorts[slot].capacity = 0;

	RELEASE_PORT_LOCK(sPorts[slot]);

	return B_NO_ERROR;
}


status_t
_kern_delete_port(port_id id)
{
	sem_id readSem, writeSem, portSem;
	int slot;

	if (!sPortsActive)
		port_init();
	if (!sPortsActive || id < 0)
		return B_BAD_PORT_ID;

	slot = id % gMaxPorts;

	GRAB_PORT_LOCK(sPorts[slot]);

	if (sPorts[slot].id != id) {
		RELEASE_PORT_LOCK(sPorts[slot]);
		dprintf("delete_port: invalid port_id %ld\n", id);
		return B_BAD_PORT_ID;
	}

	/* mark port as invalid */
	sPorts[slot].id	= -1;
	readSem = sPorts[slot].read_sem;
	writeSem = sPorts[slot].write_sem;
	sPorts[slot].name[0] = '\0';
	portSem = sPorts[slot].lock;

	RELEASE_PORT_LOCK(sPorts[slot]);

	sPorts[slot].lock = -1;

	// free the queue
	// Not necessary on Cosmoe since allocation is static

	// release the threads that were blocking on this port by deleting the sem
	// read_port() will see the B_BAD_SEM_ID acq_sem() return value, and act accordingly
	delete_sem(portSem);
	delete_sem(readSem);
	delete_sem(writeSem);

	/* schedule our port's shared memory segment for deletion */
	shmctl(sPorts[slot].queue_shm, IPC_RMID, NULL);

	TRACE(("delete_port: removed port_id %ld\n", id));

	return B_OK;
}


port_id
_kern_find_port(const char *name)
{
	port_id portFound = B_NAME_NOT_FOUND;
	int i;

	if (!sPortsActive)
		port_init();
	if (!sPortsActive)
		return B_NAME_NOT_FOUND;
	if (name == NULL)
		return B_BAD_VALUE;

	// Since we have to check every single port, and we don't
	// care if it goes away at any point, we're only grabbing
	// the port lock in question, not the port list lock

	// loop over list
	TRACE(("find_port(): Looking for port named \"%s\"\n", name));
	for (i = 0; i < gMaxPorts && portFound < B_OK; i++) {
		// lock every individual port before comparing
		GRAB_PORT_LOCK(sPorts[i]);

		if (sPorts[i].id >= 0 && !strcmp(name, sPorts[i].name))
				portFound = sPorts[i].id;

		RELEASE_PORT_LOCK(sPorts[i]);
	}

	if (portFound >= 0) {
		TRACE(("find_port(): Port %ld matches search\n", portFound));
	} else {
		TRACE(("find_port(): Couldn't find port named \"%s\"\n", name));
	}

	return portFound;
}


/** Fills the port_info structure with information from the specified
 *	port.
 *	The port lock must be held when called.
 */

static void
fill_port_info(struct port_entry *port, port_info *info, size_t size)
{
	int32 count;

	info->port = port->id;
	info->team = port->owner;
	info->capacity = port->capacity;

	get_sem_count(port->read_sem, &count);
	if (count < 0)
		count = 0;

	info->queue_count = count;
	info->total_count = port->total_count;

	strncpy(info->name, port->name, B_OS_NAME_LENGTH);
}


status_t
_kern_get_port_info(port_id id, port_info *info, size_t size)
{
	int slot;

	if (info == NULL || size != sizeof(port_info))
		return B_BAD_VALUE;
	if (!sPortsActive)
		port_init();
	if (!sPortsActive || id < 0)
		return B_BAD_PORT_ID;

	slot = id % gMaxPorts;

	GRAB_PORT_LOCK(sPorts[slot]);

	if (sPorts[slot].id != id || sPorts[slot].capacity == 0) {
		RELEASE_PORT_LOCK(sPorts[slot]);
		TRACE(("get_port_info: invalid port_id %ld\n", id));
		return B_BAD_PORT_ID;
	}

	// fill a port_info struct with info
	fill_port_info(&sPorts[slot], info, size);

	RELEASE_PORT_LOCK(sPorts[slot]);

	return B_OK;
}


status_t
_kern_get_port_message_info_etc(port_id port, port_message_info *info,
	size_t infoSize, uint32 flags, bigtime_t timeout)
{
	TRACE("UNIMPLEMENTED\n");
	return B_ERROR;
}


status_t
_kern_get_next_port_info(team_id team, int32 *_cookie, struct port_info *info, size_t size)
{
	int slot;

	if (info == NULL || size != sizeof(port_info) || _cookie == NULL || team < B_OK)
		return B_BAD_VALUE;
	if (!sPortsActive)
		port_init();
	if (!sPortsActive)
		return B_BAD_PORT_ID;

	slot = *_cookie;
	if (slot >= gMaxPorts)
		return B_BAD_PORT_ID;

	if (team == B_CURRENT_TEAM)
		team = team_get_current_team_id();

	info->port = -1; // used as found flag

	GRAB_PORT_LIST_LOCK();

	while (slot < gMaxPorts) {
		GRAB_PORT_LOCK(sPorts[slot]);
		if (sPorts[slot].id != -1 && sPorts[slot].capacity != 0 && sPorts[slot].owner == team) {
			// found one!
			fill_port_info(&sPorts[slot], info, size);

			RELEASE_PORT_LOCK(sPorts[slot]);
			slot++;
			break;
		}
		RELEASE_PORT_LOCK(sPorts[slot]);
		slot++;
	}
	RELEASE_PORT_LIST_LOCK();

	if (info->port == -1)
		return B_BAD_PORT_ID;

	*_cookie = slot;
	return B_NO_ERROR;
}


ssize_t
_kern_port_buffer_size(port_id id)
{
	return port_buffer_size_etc(id, 0, 0);
}


ssize_t
_kern_port_buffer_size_etc(port_id id, uint32 flags, bigtime_t timeout)
{
	sem_id cachedSem;
	status_t status;
	port_msg *msg;
	ssize_t size;
	int slot;
	int tail;
	void* msg_queue;

	TRACE(("port_buffer_size(%ld): enter\n", (long)id));

	if (!sPortsActive)
		port_init();

	if (!sPortsActive || id < 0)
		return B_BAD_PORT_ID;

	slot = id % gMaxPorts;

	GRAB_PORT_LOCK(sPorts[slot]);

	if (sPorts[slot].id != id) {
		RELEASE_PORT_LOCK(sPorts[slot]);
		TRACE(("get_buffer_size_etc: invalid port_id %ld\n", id));
		return B_BAD_PORT_ID;
	}

	cachedSem = sPorts[slot].read_sem;

	RELEASE_PORT_LOCK(sPorts[slot]);

	// block if no message, or, if B_TIMEOUT flag set, block with timeout

	status = acquire_sem_etc(cachedSem, 1, flags, timeout);

	if (status == B_BAD_SEM_ID) {
		// somebody deleted the port
		return B_BAD_PORT_ID;
	}
	if (status == B_TIMED_OUT || status == B_WOULD_BLOCK)
		return status;

	GRAB_PORT_LOCK(sPorts[slot]);

	if (sPorts[slot].id != id) {
		// the port is no longer there
		RELEASE_PORT_LOCK(sPorts[slot]);
		return B_BAD_PORT_ID;
	}

	// determine tail & get the length of the message
	tail = sPorts[slot].tail;
	if (tail < 0)
		panic("port %ld: tail < 0", sPorts[slot].id);
	if (tail > sPorts[slot].capacity)
		panic("port %ld: tail > cap %ld", sPorts[slot].id, sPorts[slot].capacity);

	msg_queue = shmat(sPorts[slot].queue_shm, NULL, 0);
	if (msg_queue == (void *) -1)
		panic("port %ld: missing queue", sPorts[slot].id);

	msg = msg_queue + (sizeof(port_msg) * tail);
	if (msg == NULL)
		panic("port %ld: no messages found", sPorts[slot].id);

	size = msg->size;

	shmdt(msg_queue);

	RELEASE_PORT_LOCK(sPorts[slot]);

	// restore read_sem, as we haven't read from the port
	release_sem(cachedSem);

	// return length of item at end of queue
	return size;
}


ssize_t
_kern_port_count(port_id id)
{
	int32 count;
	int slot;

	if (!sPortsActive == false)
		port_init();
	if (!sPortsActive || id < 0)
		return B_BAD_PORT_ID;

	slot = id % gMaxPorts;

	GRAB_PORT_LOCK(sPorts[slot]);

	if (sPorts[slot].id != id) {
		RELEASE_PORT_LOCK(sPorts[slot]);
		TRACE(("port_count: invalid port_id %ld\n", id));
		return B_BAD_PORT_ID;
	}

	get_sem_count(sPorts[slot].read_sem, &count);
	// do not return negative numbers 
	if (count < 0)
		count = 0;

	RELEASE_PORT_LOCK(sPorts[slot]);

	// return count of messages (sem_count)
	return count;
}

ssize_t
_kern_read_port(port_id port, int32 *msgCode, void *msgBuffer, size_t bufferSize)
{
	return read_port_etc(port, msgCode, msgBuffer, bufferSize, 0, 0);
}


ssize_t
_kern_read_port_etc(port_id id, int32 *_msgCode, void *msgBuffer, size_t bufferSize,
	uint32 flags, bigtime_t timeout)
{
	sem_id cachedSem;
	status_t status;
	port_msg *msg;
	size_t size;
	int slot;
	int tail;
	void* msg_queue;

	if (!sPortsActive)
		port_init();

	if (!sPortsActive || id < 0)
		return B_BAD_PORT_ID;

	if (_msgCode == NULL
		|| (msgBuffer == NULL && bufferSize > 0)
		|| timeout < 0)
		return B_BAD_VALUE;

	flags = flags & (B_CAN_INTERRUPT | B_TIMEOUT | B_RELATIVE_TIMEOUT |
		B_ABSOLUTE_TIMEOUT);
	slot = id % gMaxPorts;

	GRAB_PORT_LOCK(sPorts[slot]);

	if (sPorts[slot].id != id) {
		RELEASE_PORT_LOCK(sPorts[slot]);
		dprintf("read_port_etc: invalid port_id %ld\n", id);
		return B_BAD_PORT_ID;
	}
	// store sem_id in local variable
	cachedSem = sPorts[slot].read_sem;

	// unlock port && enable ints/
	RELEASE_PORT_LOCK(sPorts[slot]);
	TRACE(("read_port_etc: about to acquire read sem\n"));

	status = acquire_sem_etc(cachedSem, 1, flags, timeout);
		// get 1 entry from the queue, block if needed

	if (status == B_BAD_SEM_ID || status == B_INTERRUPTED) {
		/* somebody deleted the port or the sem went away */
		return B_BAD_PORT_ID;
	}

	if (status == B_TIMED_OUT || status == B_WOULD_BLOCK)
		return status;

	if (status != B_NO_ERROR) {
		dprintf("read_port_etc: unknown error %ld\n", status);
		return status;
	}

	GRAB_PORT_LOCK(sPorts[slot]);

	// first, let's check if the port is still alive
	if (sPorts[slot].id == -1) {
		// the port has been deleted in the meantime
		RELEASE_PORT_LOCK(sPorts[slot]);
		return B_BAD_PORT_ID;
	}

	tail = sPorts[slot].tail;
	if (tail < 0)
		panic("port %ld: tail < 0", sPorts[slot].id);
	if (tail > sPorts[slot].capacity)
		panic("port %ld: tail > cap %ld", sPorts[slot].id, sPorts[slot].capacity);

	sPorts[slot].tail = (sPorts[slot].tail + 1) % sPorts[slot].capacity;

	msg_queue = shmat(sPorts[slot].queue_shm, NULL, 0);
	if (msg_queue == (void *) -1)
		panic("port %ld: missing queue", sPorts[slot].id);

	msg = msg_queue + (sizeof(port_msg) * tail);
	if (msg == NULL)
		panic("port %ld: no messages found", sPorts[slot].id);

	sPorts[slot].total_count++;

	cachedSem = sPorts[slot].write_sem;

	RELEASE_PORT_LOCK(sPorts[slot]);

	// check output buffer size
	size = min(bufferSize, msg->size);

	// copy message
	*_msgCode = msg->code;
	if (size > 0) {
		if (msgBuffer)
			memcpy(msgBuffer, msg->buffer_chain, size);
	}
	put_port_msg(msg);

	// make one spot in queue available again for write
	release_sem(cachedSem);
		// ToDo: we might think about setting B_NO_RESCHEDULE here
		//	from time to time (always?)

	TRACE(("read_port_etc(): read %ld bytes from port %ld queue position %d.\n", (long)size, id, tail));
	return size;
}


status_t
_kern_write_port(port_id id, int32 msgCode, const void *msgBuffer, size_t bufferSize)
{
	return write_port_etc(id, msgCode, msgBuffer, bufferSize, 0, 0);
}


status_t
_kern_write_port_etc(port_id id, int32 msgCode, const void *msgBuffer,
	size_t bufferSize, uint32 flags, bigtime_t timeout)
{
	sem_id cachedSem;
	status_t status;
	port_msg *msg;
	int head;
	int slot;
	void* msg_queue;

	if (!sPortsActive)
		port_init();

	if (!sPortsActive || id < 0)
		return B_BAD_PORT_ID;

	// mask irrelevant flags (for acquire_sem() usage)
	flags = flags & (B_CAN_INTERRUPT | B_TIMEOUT | B_RELATIVE_TIMEOUT |
		B_ABSOLUTE_TIMEOUT);
	slot = id % gMaxPorts;

	if (bufferSize > PORT_MAX_MESSAGE_SIZE)
		return EINVAL;

	GRAB_PORT_LOCK(sPorts[slot]);

	if (sPorts[slot].id != id) {
		RELEASE_PORT_LOCK(sPorts[slot]);
		TRACE(("write_port_etc: invalid port_id %ld\n", id));
		return B_BAD_PORT_ID;
	}

	if (sPorts[slot].capacity == 0) {
		RELEASE_PORT_LOCK(sPorts[slot]);
		TRACE(("write_port_etc: port %ld closed\n", id));
		return B_BAD_PORT_ID;
	}

	// store sem_id in local variable 
	cachedSem = sPorts[slot].write_sem;

	RELEASE_PORT_LOCK(sPorts[slot]);

	status = acquire_sem_etc(cachedSem, 1, flags, timeout);
		// get 1 entry from the queue, block if needed

	if (status == B_BAD_SEM_ID || status == B_INTERRUPTED) {
		/* somebody deleted the port or the sem while we were waiting */
		return B_BAD_PORT_ID;
	}

	if (status == B_TIMED_OUT || status == B_WOULD_BLOCK)
		return status;

	if (status != B_NO_ERROR) {
		dprintf("write_port_etc: unknown error %ld\n", status);
		return status;
	}

	// Find and sanity-check the head of the queue
	head = sPorts[slot].head;
	if (head < 0)
		panic("port %ld: head < 0", sPorts[slot].id);
	if (head >= sPorts[slot].capacity)
		panic("port %ld: head > cap %ld", sPorts[slot].id, sPorts[slot].capacity);

	msg_queue = shmat(sPorts[slot].queue_shm, NULL, 0);
	if (msg_queue == (void *) -1)
		panic("port %ld: missing queue", sPorts[slot].id);

	msg = msg_queue + (sizeof(port_msg) * head);

	msg->code = msgCode;
	msg->size = bufferSize;
	memcpy(msg->buffer_chain, msgBuffer, bufferSize);
	sPorts[slot].head = (sPorts[slot].head + 1) % sPorts[slot].capacity;

	shmdt(msg_queue);

	// attach message to queue
	GRAB_PORT_LOCK(sPorts[slot]);

	// first, let's check if the port is still alive
	if (sPorts[slot].id == -1) {
		// the port has been deleted in the meantime
		RELEASE_PORT_LOCK(sPorts[slot]);

		//put_port_msg(msg);
		return B_BAD_PORT_ID;
	}

	// list_add_item not necessary, already done in Cosmoe

	// store sem_id in local variable 
	cachedSem = sPorts[slot].read_sem;

	RELEASE_PORT_LOCK(sPorts[slot]);

	// release sem, allowing read (might reschedule)
	release_sem(cachedSem);

	TRACE(("write_port_etc(): wrote %ld bytes to port %d queue position %d.\n", (long)bufferSize, slot, head));
	return B_NO_ERROR;
}


status_t
_kern_set_port_owner(port_id id, team_id team)
{
	int slot;

	if (!sPortsActive)
		port_init();

	if (!sPortsActive || id < 0)
		return B_BAD_PORT_ID;

	slot = id % gMaxPorts;

	GRAB_PORT_LOCK(sPorts[slot]);

	if (sPorts[slot].id != id) {
		RELEASE_PORT_LOCK(sPorts[slot]);
		TRACE(("set_port_owner: invalid port_id %ld\n", id));
		return B_BAD_PORT_ID;
	}

	// transfer ownership to other team
	sPorts[slot].owner = team;

	// unlock port
	RELEASE_PORT_LOCK(sPorts[slot]);

	return B_NO_ERROR;
}

//	#pragma mark -
/* 
 *	private functions
 */

void teardown_ports(void)
{
	if (!sPorts) {
		printf("teardown_ports(): no ports to delete\n");
		return;
	}

	// remove all sems owned by our team
	int num_deleted = delete_owned_ports(getpid());

	printf("teardown_ports(): %d ports deleted\n", num_deleted);
}

