/*------------------------------------------------------------------------------
//	Copyright (c) 2003, Tom Marshall
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
//	File Name:		OS.cpp
//	Authors:		Tom Marshall (tommy@tig-grr.com)
//----------------------------------------------------------------------------*/


#include <SupportDefs.h>
#include <StorageDefs.h> // Just because BeOS apps expect this here
#include <OS.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <string.h>


#define TRACE_AREA 0
#if TRACE_AREA
#	define TRACE(x) dprintf x
#	define TRACE_BLOCK(x) dprintf x
#else
#	define TRACE(x) ;
#	define TRACE_BLOCK(x) ;
#endif

#define dprintf printf

// Area IDs
#define AREA_ID_MAX  256
#define AREA_ID_FREE 0xFFFFFFFF


area_info* g_pAreaMap = NULL;


void init_area_map(void)
{
	int shmid;
	int created = 1;

	/* create a unique key for our system-wide sem table */
	key_t table_key = ftok("/usr/local/bin/", (int)'A');

	TRACE(("Master area table key is 0x%x.\n", table_key));

	/* create and initialize a new area table in shared memory */
	shmid = shmget(table_key, sizeof(area_info)*AREA_ID_MAX,
		IPC_CREAT | IPC_EXCL | 0700);

	if (shmid == -1 && errno == EEXIST) {
		/* grab the existing shared memory semaphore table */
		shmid = shmget(table_key,
			sizeof(area_info)*AREA_ID_MAX, IPC_CREAT | 0700);

		TRACE(("Using existing system area table.\n"));

		created = 0;
	}

	g_pAreaMap = shmat(shmid, NULL, 0);
	if (g_pAreaMap == (void*)(-1)) {
		printf("init_area_map(): failed in shmat: %s\n",
			strerror(errno));
		g_pAreaMap = NULL;
		return;
	}

	if (created == 1) {
		for (uint32 n = 0; n < AREA_ID_MAX; n++) {
			g_pAreaMap[n].area = AREA_ID_FREE;
		}
	}
}


/* TODO: need to sync access to area map with mutex */
area_id _kern_create_area(const char* name, void** start_addr,
	uint32 addr_spec, size_t size, uint32 lock, uint32 protection)
{
	if (g_pAreaMap == NULL)
		init_area_map();

	for (uint32 n = 0; n < AREA_ID_MAX; n++) {
		if (g_pAreaMap[n].area == AREA_ID_FREE) {
			int iShmID = shmget(n, size, IPC_CREAT | 0700);
			if (iShmID == -1) {
				printf("create_area(): shmget(%u,%u) failed (%s)\n",
					n, size, strerror(errno));
				return B_NO_MEMORY;
			}
			
			g_pAreaMap[n].address = shmat(iShmID, NULL, 0);
			if (g_pAreaMap[n].address == (void*)(-1)) {
				printf("create_area(): shmat(%d) failed (%s)\n",
					iShmID, strerror(errno));
				return B_NO_MEMORY;
			}
			
			if (start_addr != NULL) {
				*start_addr = g_pAreaMap[n].address;
			}
			strncpy(g_pAreaMap[n].name, name, B_OS_NAME_LENGTH);
			g_pAreaMap[n].name[B_OS_NAME_LENGTH -1] = '\0';
			g_pAreaMap[n].area = iShmID;
			g_pAreaMap[n].size = size;
			g_pAreaMap[n].lock = lock;
			g_pAreaMap[n].protection = protection;
			g_pAreaMap[n].team = getpid();
			g_pAreaMap[n].ram_size = size;
			return n;
		}
	}
	return B_NO_MEMORY;
}


area_id _kern_clone_area(const char* name, void** dest_addr,
	uint32 addr_spec, uint32 protection, area_id source)
{
	if (g_pAreaMap == NULL)
		init_area_map();

	if (source < 0 || source >= AREA_ID_MAX || g_pAreaMap == NULL ||
		g_pAreaMap[source].area == AREA_ID_FREE) {
		printf("clone_area(): AREA IS FREE\n");
		return -EPERM;
	}

	size_t nSize = g_pAreaMap[source].size;
	uint32 nProtection = g_pAreaMap[source].protection;
	uint32 lock = g_pAreaMap[source].lock;

	for (uint32 n = 0; n < AREA_ID_MAX; n++) {
		if (g_pAreaMap[n].area == AREA_ID_FREE) {
			int iShmID = shmget(source, nSize, IPC_CREAT | 0700);
			if (iShmID == -1) {
				printf("clone_area(): shmget(%u,%u) failed (%s)\n", n, nSize, strerror(errno));
				return B_NO_MEMORY;
			}

			g_pAreaMap[n].address = shmat(iShmID, NULL, 0);

			if (g_pAreaMap[n].address == (void*)(-1)) {
				printf("clone_area(): shmat(%d) failed (%s)\n", iShmID, strerror(errno));
				return B_NO_MEMORY;
			}

			if (dest_addr != NULL) {
				*dest_addr = g_pAreaMap[n].address;
			}
			strncpy(g_pAreaMap[n].name, name, B_OS_NAME_LENGTH);
			g_pAreaMap[n].name[B_OS_NAME_LENGTH -1] = '\0';
			g_pAreaMap[n].area = iShmID;
			g_pAreaMap[n].size = nSize;
			g_pAreaMap[n].lock = lock;
			g_pAreaMap[n].protection = nProtection;
			g_pAreaMap[n].team = getpid();
			g_pAreaMap[n].ram_size = nSize;
			return n;
		}
	}

	return B_NO_MEMORY;
}


area_id
_kern_find_area(const char *name)
{
	if (g_pAreaMap == NULL)
		init_area_map();

	for (uint32 n = 0; n < AREA_ID_MAX; n++) {
		if (g_pAreaMap[n].area != AREA_ID_FREE
				&& strcmp(name, g_pAreaMap[n].name) == 0) {
			return n;
		}
	}

	return B_ERROR;
}


area_id
_kern_area_for (void *address)
{
	if (g_pAreaMap == NULL)
		init_area_map();

	for (uint32 n = 0; n < AREA_ID_MAX; n++) {
		if (g_pAreaMap[n].area != AREA_ID_FREE) {
			if ((address >= g_pAreaMap[n].address)
				&& (address < g_pAreaMap[n].address + g_pAreaMap[n].size)) {
				return n;
			}
		}
	}

	return B_ERROR;
}


status_t _kern_delete_area(area_id hArea)
{
	if (hArea < 0 || hArea >= AREA_ID_MAX || g_pAreaMap == NULL ||
			g_pAreaMap[hArea].area == AREA_ID_FREE) {
		return B_ERROR;
	}
	/* FIXME: refcount the shmseg and shmdt() when zero */
	g_pAreaMap[hArea].area = AREA_ID_FREE;

	return 0;
}


status_t _kern_resize_area(area_id id, size_t new_size)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t _kern_get_area_info(area_id hArea, area_info* psInfo, size_t size)
{
	if (hArea < 0 || hArea >= AREA_ID_MAX || g_pAreaMap == NULL ||
			g_pAreaMap[hArea].area == AREA_ID_FREE) {
		return B_BAD_VALUE;
	}
	
	*psInfo = g_pAreaMap[hArea];
	return B_OK;
}


status_t
_kern_get_next_area_info(team_id team, int32 *cookie, area_info *areaInfo, size_t size)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_set_area_protection(area_id id, uint32 protection)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


area_id
_kern_transfer_area(area_id area, void **_address, uint32 addressSpec,
	team_id target)
{
	UNIMPLEMENTED();
	return -1;
}


status_t
_kern_reserve_address_range(addr_t* _address,
	uint32 addressSpec, addr_t size)
{
	UNIMPLEMENTED();
	return B_ERROR;
}
