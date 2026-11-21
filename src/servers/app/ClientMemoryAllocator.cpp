/*
 * Copyright 2006-2013, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */


/*!	This class manages a pool of areas for one client. The client is supposed
	to clone these areas into its own address space to access the data.
	This mechanism is only used for bitmaps for far.
*/


// TODO: areas could be relocated if needed (to be able to resize them)
//		However, this would require a lock whenever a block of memory
//		allocated by this allocator is accessed.


#include "ClientMemoryAllocator.h"

#include <stdio.h>
#include <stdlib.h>

#include <Autolock.h>
#include <OS.h>

#ifdef HAVE_GBM
#include <gbm.h>
#endif

#include <BufferAllocator.h>

#include "ServerApp.h"
#include "ServerMemoryAllocator.h"


typedef block_list::Iterator block_iterator;
typedef chunk_list::Iterator chunk_iterator;


ClientMemoryAllocator::ClientMemoryAllocator(ServerApp* application)
	:
	fApplication(application),
	fLock("client memory lock")
{
}


ClientMemoryAllocator::~ClientMemoryAllocator()
{
	// delete all areas and chunks/blocks that are still allocated

	while (true) {
		struct block* block = fFreeBlocks.RemoveHead();
		if (block == NULL)
			break;

		free(block);
	}

	while (true) {
		struct chunk* chunk = fChunks.RemoveHead();
		if (chunk == NULL)
			break;

		delete_area(chunk->area);
		free(chunk);
	}
}


void*
ClientMemoryAllocator::Allocate(size_t size, block** _address)
{
	// A detached allocator no longer allows any further allocations
	if (fApplication == NULL)
		return NULL;

	BAutolock locker(fLock);

	// Search best matching free block from the list

	block_iterator iterator = fFreeBlocks.GetIterator();
	struct block* block;
	struct block* best = NULL;

	while ((block = iterator.Next()) != NULL) {
		if (block->size >= size && (best == NULL || block->size < best->size))
			best = block;
	}

	if (best == NULL) {
		// We didn't find a free block - we need to allocate
		// another chunk, or resize an existing chunk
		best = _AllocateChunk(size);
		if (best == NULL)
			return NULL;
	}

	// We need to split the chunk into two parts: the one to keep
	// and the one to give away

	if (best->size == size) {
		// The simple case: the free block has exactly the size we wanted to have
		fFreeBlocks.Remove(best);
		*_address = best;
		return best->base;
	}

	// TODO: maybe we should have the user reserve memory in its object
	//	for us, so we don't have to do this here...

	struct block* usedBlock = (struct block*)malloc(sizeof(struct block));
	if (usedBlock == NULL)
		return NULL;

	usedBlock->base = best->base;
	usedBlock->size = size;
	usedBlock->chunk = best->chunk;

	best->base += size;
	best->size -= size;

	*_address = usedBlock;
	return usedBlock->base;
}


void
ClientMemoryAllocator::Free(block* freeBlock)
{
	if (freeBlock == NULL)
		return;

	BAutolock locker(fLock);

	// search for an adjacent free block

	block_iterator iterator = fFreeBlocks.GetIterator();
	struct block* before = NULL;
	struct block* after = NULL;
	bool inFreeList = true;

	if (freeBlock->size != freeBlock->chunk->size) {
		// TODO: this could be done better if free blocks are sorted,
		//	and if we had one free blocks list per chunk!
		//	IOW this is a bit slow...

		while (struct block* block = iterator.Next()) {
			if (block->chunk != freeBlock->chunk)
				continue;

			if (block->base + block->size == freeBlock->base)
				before = block;

			if (block->base == freeBlock->base + freeBlock->size)
				after = block;
		}

		if (before != NULL && after != NULL) {
			// merge with adjacent blocks
			before->size += after->size + freeBlock->size;
			fFreeBlocks.Remove(after);
			free(after);
			free(freeBlock);
			freeBlock = before;
		} else if (before != NULL) {
			before->size += freeBlock->size;
			free(freeBlock);
			freeBlock = before;
		} else if (after != NULL) {
			after->base -= freeBlock->size;
			after->size += freeBlock->size;
			free(freeBlock);
			freeBlock = after;
		} else
			fFreeBlocks.Add(freeBlock);
	} else
		inFreeList = false;

	if (freeBlock->size == freeBlock->chunk->size) {
		// We can delete the chunk now
		struct chunk* chunk = freeBlock->chunk;

		if (inFreeList)
			fFreeBlocks.Remove(freeBlock);
		free(freeBlock);

		fChunks.Remove(chunk);
		delete_area(chunk->area);

		free(chunk);
	}
}


void
ClientMemoryAllocator::Detach()
{
	BAutolock locker(fLock);
	fApplication = NULL;
}


void
ClientMemoryAllocator::Dump()
{
	if (fApplication != NULL) {
		debug_printf("Application %" B_PRId32 ", %s: chunks:\n",
			fApplication->ClientTeam(), fApplication->Signature());
	}

	chunk_list::Iterator iterator = fChunks.GetIterator();
	int32 i = 0;
	while (struct chunk* chunk = iterator.Next()) {
		debug_printf("  [%4" B_PRId32 "] %p, area %" B_PRId32 ", base %p, "
			"size %lu\n", i++, chunk, chunk->area, chunk->base, chunk->size);
	}

	debug_printf("free blocks:\n");

	block_list::Iterator blockIterator = fFreeBlocks.GetIterator();
	i = 0;
	while (struct block* block = blockIterator.Next()) {
		debug_printf("  [%6" B_PRId32 "] %p, chunk %p, base %p, size %lu\n",
			i++, block, block->chunk, block->base, block->size);
	}
}


struct block*
ClientMemoryAllocator::_AllocateChunk(size_t size)
{
	// round up to multiple of page size
	size = (size + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1);

	// Skip resize_area entirely: when the server grows a chunk, the
	// client's ServerMemoryAllocator returns its cached (original-size)
	// clone_area mapping for any subsequent bitmap in that chunk, so a
	// bitmap placed past the original chunk size lands outside the
	// client's mapping and faults on access. Always allocate a fresh
	// chunk; modest area overhead, no protocol changes needed.
	struct chunk* chunk = NULL;
	struct block* block;
	uint8* address;

	if (chunk == NULL) {
		// TODO: temporary measurement as long as resizing areas doesn't
		//	work the way we need (with relocating the area, if needed)
		if (size < B_PAGE_SIZE * 32)
			size = B_PAGE_SIZE * 32;

		// create new area for this allocation
		chunk = (struct chunk*)malloc(sizeof(struct chunk));
		if (chunk == NULL)
			return NULL;

		block = (struct block*)malloc(sizeof(struct block));
		if (block == NULL) {
			free(chunk);
			return NULL;
		}

		char name[B_OS_NAME_LENGTH];
#ifdef HAIKU_TARGET_PLATFORM_LIBBE_TEST
		strcpy(name, "client heap");
#else
		snprintf(name, sizeof(name), "heap:%" B_PRId32 ":%s",
			fApplication->ClientTeam(), fApplication->SignatureLeaf());
#endif
		area_id area = create_area(name, (void**)&address, B_ANY_ADDRESS, size,
			B_NO_LOCK, B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);
		if (area < B_OK) {
			free(block);
			free(chunk);
			return NULL;
		}

		// add chunk to list

		chunk->area = area;
		chunk->base = address;
		chunk->size = size;

		fChunks.Add(chunk);
	} else {
		// create new free block for this chunk
		block = (struct block *)malloc(sizeof(struct block));
		if (block == NULL)
			return NULL;

		address = chunk->base + chunk->size;
		chunk->size += size;
	}

	// add block to free list

	block->chunk = chunk;
	block->base = address;
	block->size = size;

	fFreeBlocks.Add(block);

	return block;
}


// #pragma mark -


ClientMemory::ClientMemory()
	:
	fAllocator(NULL),
	fBlock(NULL)
{
}


ClientMemory::~ClientMemory()
{
	if (fAllocator != NULL) {
		if (fBlock != NULL)
			fAllocator->Free(fBlock);
		fAllocator.Unset();
	}
}


void*
ClientMemory::Allocate(ClientMemoryAllocator* allocator, size_t size)
{
	fAllocator.SetTo(allocator, false);

	return fAllocator->Allocate(size, &fBlock);
}


area_id
ClientMemory::Area()
{
	if (fBlock != NULL)
		return fBlock->chunk->area;
	return B_ERROR;
}


uint8*
ClientMemory::Address()
{
	if (fBlock != NULL)
		return fBlock->base;
	return 0;
}


size_t
ClientMemory::AreaOffset()
{
	if (fBlock != NULL)
		return fBlock->base - fBlock->chunk->base;
	return 0;
}


// #pragma mark -


static BLocker sLocker("ClonedAreaMemory allocator");
static BPrivate::ServerMemoryAllocator sClonedAreaMemoryAllocator;


ClonedAreaMemory::ClonedAreaMemory()
	:
	fArea(-1),
	fClonedArea(-1),
	fOffset(0),
	fBase(NULL)
{
}


ClonedAreaMemory::~ClonedAreaMemory()
{
	BAutolock locker(sLocker);
	sClonedAreaMemoryAllocator.RemoveArea(fArea);
}


void*
ClonedAreaMemory::Clone(area_id area, size_t offset)
{
	BAutolock locker(sLocker);
	if (sClonedAreaMemoryAllocator.AddArea(area, fClonedArea, fBase, 0, false) != B_OK)
		return NULL;

	fArea = area;
	fOffset = offset;
	return Address();
}


area_id
ClonedAreaMemory::Area()
{
	return fClonedArea;
}


uint8*
ClonedAreaMemory::Address()
{
	return fBase + fOffset;
}


size_t
ClonedAreaMemory::AreaOffset()
{
	return fOffset;
}


#ifdef HAVE_GBM

GbmMemory::GbmMemory()
	:
	fBo(NULL),
	fMapData(NULL),
	fArea(-1),
	fBase(NULL),
	fKeepModifier(false)
{
}


GbmMemory::~GbmMemory()
{
	if (fArea >= 0)
		delete_area(fArea);

	if (fBo != NULL) {
		if (fMapData != NULL)
			BPrivate::BufferAllocator::Unmap(fBo, fMapData);

		BPrivate::BufferAllocator::Destroy(fBo);
	}
}


/*!	Takes ownership of the buffer object, its mapping cookie and the
	dma-buf backed area. Any NULL input aborts the adoption and the
	caller keeps ownership of what was not adopted.
*/
status_t
GbmMemory::Adopt(struct gbm_bo* bo, area_id area, uint8* base, void* mapData)
{
	if (fBo != NULL || bo == NULL || (area < B_OK) != (base == NULL))
		return B_BAD_VALUE;

	fBo = bo;
	fArea = area >= B_OK ? area : -1;
	fBase = base;
	fMapData = mapData;
	return B_OK;
}


area_id
GbmMemory::Area()
{
	return fArea;
}


uint8*
GbmMemory::Address()
{
	return fBase;
}


size_t
GbmMemory::AreaOffset()
{
	return 0;
}


int
GbmMemory::ExportFd() const
{
	return BPrivate::BufferAllocator::Export(fBo);
}


uint32
GbmMemory::Width() const
{
	return fBo != NULL ? gbm_bo_get_width(fBo) : 0;
}


uint32
GbmMemory::Height() const
{
	return fBo != NULL ? gbm_bo_get_height(fBo) : 0;
}


uint32
GbmMemory::Stride() const
{
	return fBo != NULL ? gbm_bo_get_stride(fBo) : 0;
}


uint32
GbmMemory::Format() const
{
	return fBo != NULL ? gbm_bo_get_format(fBo) : 0;
}


uint64
GbmMemory::Modifier() const
{
	if (fBo == NULL)
		return 0;

	// A buffer made without a list is linear, whatever the driver reports.
	const uint64 modifier = gbm_bo_get_modifier(fBo);
	return modifier == 0x00ffffffffffffffULL && !fKeepModifier ? 0 : modifier;
}


bool
GbmMemory::IsLinear() const
{
	return Modifier() == 0;
}


uint32
GbmMemory::Offset() const
{
	return fBo != NULL ? gbm_bo_get_offset(fBo, 0) : 0;
}


uint32
GbmMemory::Handle() const
{
	return fBo != NULL ? gbm_bo_get_handle(fBo).u32 : 0;
}


int
GbmMemory::DeviceFd() const
{
	return fBo != NULL ? gbm_device_get_fd(gbm_bo_get_device(fBo)) : -1;
}

#endif	// HAVE_GBM
