/*
 * Copyright 2006-2013, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */
#ifndef CLIENT_MEMORY_ALLOCATOR_H
#define CLIENT_MEMORY_ALLOCATOR_H


#include <Locker.h>
#include <Referenceable.h>

#include <util/DoublyLinkedList.h>


class ServerApp;
struct chunk;
struct block;

struct chunk : DoublyLinkedListLinkImpl<struct chunk> {
	area_id	area;
	uint8*	base;
	size_t	size;
};

struct block : DoublyLinkedListLinkImpl<struct block> {
	struct chunk* chunk;
	uint8*	base;
	size_t	size;
};

typedef DoublyLinkedList<block> block_list;
typedef DoublyLinkedList<chunk> chunk_list;


class ClientMemoryAllocator : public BReferenceable {
public:
								ClientMemoryAllocator(ServerApp* application);
								~ClientMemoryAllocator();

			void*				Allocate(size_t size, block** _address);
			void				Free(block* cookie);

			void				Detach();

			void				Dump();

private:
			struct block*		_AllocateChunk(size_t size);

private:
			ServerApp*			fApplication;
			BLocker				fLock;
			chunk_list			fChunks;
			block_list			fFreeBlocks;
};


class AreaMemory {
public:
	virtual						~AreaMemory() {}

	virtual area_id				Area() = 0;
	virtual uint8*				Address() = 0;
	virtual size_t				AreaOffset() = 0;
};


class ClientMemory : public AreaMemory {
public:
								ClientMemory();

	virtual						~ClientMemory();

			void*				Allocate(ClientMemoryAllocator* allocator,
									size_t size);

	virtual area_id				Area();
	virtual uint8*				Address();
	virtual size_t				AreaOffset();

private:
			BReference<ClientMemoryAllocator>
								fAllocator;
			block*				fBlock;
};


/*! Just clones an existing area. */
class ClonedAreaMemory : public AreaMemory {
public:
								ClonedAreaMemory();
	virtual						~ClonedAreaMemory();

			void*				Clone(area_id area, size_t offset);

	virtual area_id				Area();
	virtual uint8*				Address();
	virtual size_t				AreaOffset();

private:
			area_id		fArea;
			area_id		fClonedArea;
			size_t		fOffset;
			uint8*		fBase;
};


#ifdef HAVE_GBM
/*! Backs a ServerBitmap with a GBM buffer object. The buffer is CPU
	mapped for the server, exported as a dma-buf, and the dma-buf pages
	are placed in a cloneable area so the client maps them zero-copy
	through the ordinary bitmap area protocol. Owns the buffer object,
	the mapping cookie and the area.
 */
class GbmMemory : public AreaMemory {
public:
								GbmMemory();
	virtual						~GbmMemory();

			status_t			Adopt(struct gbm_bo* bo, area_id area,
									uint8* base, void* mapData);

	virtual	area_id				Area();
	virtual	uint8*				Address();
	virtual	size_t				AreaOffset();

			struct gbm_bo*		BufferObject() const { return fBo; }

			// dma-buf export; the caller closes the returned fd.
			int					ExportFd() const;
			uint32				Width() const;
			uint32				Height() const;
			uint32				Stride() const;
			uint32				Format() const;
			// GEM handle of the buffer object on the gbm device fd.
			uint32				Handle() const;
			// The fd of the gbm device the buffer object lives on.
			int					DeviceFd() const;

private:
			struct gbm_bo*		fBo;
			void*				fMapData;
			area_id				fArea;
			uint8*				fBase;
};
#endif	// HAVE_GBM

#endif	/* CLIENT_MEMORY_ALLOCATOR_H */
