// Unit tests for BBlockCache
//
// A BBlockCache hands out blocks of one size and keeps freed ones for reuse.
// The test tracks every block it has: blocks the cache holds for reuse, blocks
// in use, and blocks of a size the cache does not keep. It then checks that
//
//	- a block handed out is never one already in use,
//	- a block of the cache's size comes from the cache while it has one free,
//	- a block of any other size never comes from the cache.
//
// Both cache flavours are covered, over a range of block counts and sizes.

#include <BlockCache.h>
#include <List.h>

#include <stdlib.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>


namespace {

class CacheExercise {
public:
	CacheExercise(int32 blockCount, size_t blockSize, bool mallocCache)
		:
		fCache(new BBlockCache(blockCount, blockSize, mallocCache
			? B_MALLOC_CACHE : B_OBJECT_CACHE)),
		fBlockCount(blockCount),
		fBlockSize(blockSize),
		// Blocks of a size the cache does not keep
		fOtherSize(blockSize - 6),
		fMallocCache(mallocCache)
	{
	}

	~CacheExercise()
	{
		delete fCache;

		while (!fUsed.IsEmpty())
			_Free(fUsed.LastItem(), fBlockSize);
		while (!fOther.IsEmpty())
			_Free(fOther.LastItem(), fOtherSize);
	}

	//! Fills the cache, so that what it holds for reuse is known.
	void Fill()
	{
		for (int32 i = 0; i < fBlockCount; i++)
			fFree.AddItem(fCache->Get(fBlockSize));
		for (int32 i = 0; i < fBlockCount; i++)
			fCache->Save(fFree.ItemAt(i), fBlockSize);
	}

	void Run()
	{
		// Empty the cache, and then some
		for (int32 i = 0; i < fBlockCount + 10; i++)
			_Get(fBlockSize);

		// Give them all back, in reverse order
		while (!fUsed.IsEmpty())
			_Save(fUsed.LastItem(), fBlockSize);

		// Take blocks of both sizes, returning and freeing some from the
		// middle of the lists so that the most recent block is not the one
		// given back
		for (int32 i = 0; i < fBlockCount; i++) {
			_Get(fBlockSize);
			_Get(fBlockSize);
			_Get(fOtherSize);
			_Get(fOtherSize);

			_Save(fUsed.ItemAt(fUsed.CountItems() / 2), fBlockSize);
			_Save(fOther.ItemAt(fOther.CountItems() / 2), fOtherSize);

			_Get(fBlockSize);
			_Get(fBlockSize);
			_Get(fOtherSize);
			_Get(fOtherSize);

			_Free(fUsed.ItemAt(fUsed.CountItems() / 2), fBlockSize);
			_Free(fOther.ItemAt(fOther.CountItems() / 2), fOtherSize);
		}

		// Leave the cache holding something at the end of the test
		for (int32 i = 0; i < fBlockCount / 4; i++) {
			_Save(fUsed.ItemAt(fUsed.CountItems() * 2 / 3), fBlockSize);
			_Save(fOther.ItemAt(fOther.CountItems() * 2 / 3), fOtherSize);

			_Free(fUsed.ItemAt(fUsed.CountItems() / 3), fBlockSize);
			_Free(fOther.ItemAt(fOther.CountItems() / 3), fOtherSize);
		}
	}

private:
	void* _Get(size_t size)
	{
		void* block = fCache->Get(size);

		// Never a block that is already in use
		REQUIRE_FALSE(fUsed.HasItem(block));
		REQUIRE_FALSE(fOther.HasItem(block));

		if (size == fBlockSize) {
			// While the cache holds free blocks, this must be one of them
			if (fFree.CountItems() > 0)
				REQUIRE(fFree.RemoveItem(block));

			REQUIRE(fUsed.AddItem(block));
		} else {
			// A block of another size never comes from the cache
			REQUIRE_FALSE(fFree.HasItem(block));
			REQUIRE(fOther.AddItem(block));
		}

		return block;
	}

	void _Save(void* block, size_t size)
	{
		REQUIRE_FALSE(fFree.HasItem(block));

		if (size == fBlockSize) {
			// The cache keeps it if it has room
			if (fFree.CountItems() < fBlockCount)
				REQUIRE(fFree.AddItem(block));

			REQUIRE_FALSE(fOther.HasItem(block));
			REQUIRE(fUsed.RemoveItem(block));
		} else {
			REQUIRE_FALSE(fUsed.HasItem(block));
			REQUIRE(fOther.RemoveItem(block));
		}

		fCache->Save(block, size);
	}

	void _Free(void* block, size_t size)
	{
		REQUIRE_FALSE(fFree.HasItem(block));

		if (size == fBlockSize) {
			REQUIRE_FALSE(fOther.HasItem(block));
			REQUIRE(fUsed.RemoveItem(block));
		} else {
			REQUIRE_FALSE(fUsed.HasItem(block));
			REQUIRE(fOther.RemoveItem(block));
		}

		if (fMallocCache)
			free(block);
		else
			delete[] (uint8*)block;
	}

	BBlockCache*	fCache;
	int32			fBlockCount;
	size_t			fBlockSize;
	size_t			fOtherSize;
	bool			fMallocCache;

	BList			fFree;
	BList			fUsed;
	BList			fOther;
};

}	// unnamed namespace


TEST_CASE("BBlockCache: getting, saving and freeing blocks",
	"[BBlockCache][support]")
{
	const bool mallocCache = GENERATE(false, true);
	CAPTURE(mallocCache);

	for (int32 blockCount = 8; blockCount < 513; blockCount *= 2) {
		for (size_t blockSize = 13; blockSize < 9478; blockSize *= 3) {
			CAPTURE(blockCount, blockSize);

			CacheExercise exercise(blockCount, blockSize, mallocCache);
			exercise.Fill();
			exercise.Run();
		}
	}
}
