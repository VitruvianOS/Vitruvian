// Concurrency tests for BBlockCache
//
// Six threads hammer two caches at once, three on each. Every thread writes
// its own thread id into each block it is given and checks the id is still
// there when it gives the block back: another thread's id, or anything else,
// means the cache handed the same block to two threads.
//
// Each thread also keeps its own lists of the blocks it holds, so a block
// handed out twice without being returned is caught as well.

#include <BlockCache.h>
#include <List.h>
#include <OS.h>

#include <stdlib.h>

#include <catch2/catch_test_macros.hpp>

#include <ThreadedTest.h>


static const int32 kBlockCount = 128;
static const size_t kBlockSize = 23;
static const size_t kOtherSize = 29;

// Enough repetitions for the threads to overlap
static const int32 kRepeats = 8;


namespace {

//! One thread's view of a cache: the blocks it is holding.
class CacheUser {
public:
	CacheUser(BBlockCache* cache, bool mallocCache)
		:
		fCache(cache),
		fMallocCache(mallocCache),
		fThread(find_thread(NULL))
	{
	}

	void Run()
	{
		for (int32 repeat = 0; repeat < kRepeats; repeat++) {
			for (int32 i = 0; i < kBlockCount / 2; i++) {
				_Get(kBlockSize);
				_Get(kBlockSize);
				_Get(kOtherSize);
				_Get(kOtherSize);

				_Save(fBlocks.ItemAt(fBlocks.CountItems() / 2), kBlockSize);
				_Save(fOther.ItemAt(fOther.CountItems() / 2), kOtherSize);

				_Get(kBlockSize);
				_Get(kBlockSize);
				_Get(kOtherSize);
				_Get(kOtherSize);

				_Free(fBlocks.ItemAt(fBlocks.CountItems() / 2), kBlockSize);
				_Free(fOther.ItemAt(fOther.CountItems() / 2), kOtherSize);
			}

			// Give everything back, alternating between the cache and free()
			bool free = false;
			while (!fBlocks.IsEmpty()) {
				if (free)
					_Free(fBlocks.LastItem(), kBlockSize);
				else
					_Save(fBlocks.LastItem(), kBlockSize);

				free = !free;
			}

			while (!fOther.IsEmpty()) {
				if (free)
					_Free(fOther.LastItem(), kOtherSize);
				else
					_Save(fOther.LastItem(), kOtherSize);

				free = !free;
			}
		}
	}

private:
	void* _Get(size_t size)
	{
		void* block = fCache->Get(size);

		// Not a block this thread is already holding
		THREAD_REQUIRE(!fBlocks.HasItem(block));
		THREAD_REQUIRE(!fOther.HasItem(block));

		if (size == kBlockSize)
			THREAD_REQUIRE(fBlocks.AddItem(block));
		else
			THREAD_REQUIRE(fOther.AddItem(block));

		// Claim the block, so that another thread getting it at the same time
		// shows up when it is given back
		*((thread_id*)block) = fThread;

		return block;
	}

	void _Save(void* block, size_t size)
	{
		_CheckOwner(block, size);
		fCache->Save(block, size);
	}

	void _Free(void* block, size_t size)
	{
		_CheckOwner(block, size);

		if (fMallocCache)
			free(block);
		else
			delete[] (uint8*)block;
	}

	void _CheckOwner(void* block, size_t size)
	{
		// Still this thread's block?
		THREAD_REQUIRE(*((thread_id*)block) == fThread);

		if (size == kBlockSize) {
			THREAD_REQUIRE(fBlocks.RemoveItem(block));
			THREAD_REQUIRE(!fOther.HasItem(block));
		} else {
			THREAD_REQUIRE(!fBlocks.HasItem(block));
			THREAD_REQUIRE(fOther.RemoveItem(block));
		}
	}

	BBlockCache*	fCache;
	bool			fMallocCache;
	thread_id		fThread;

	BList			fBlocks;
	BList			fOther;
};

}	// unnamed namespace


TEST_CASE("BBlockCache: several threads using one cache",
	"[BBlockCache][support]")
{
	BBlockCache objectCache(kBlockCount, kBlockSize, B_OBJECT_CACHE);
	BBlockCache mallocCache(kBlockCount, kBlockSize, B_MALLOC_CACHE);

	ThreadedTest test;

	for (int i = 0; i < 3; i++) {
		test.AddThread("object " + std::to_string(i), [&objectCache]() {
			CacheUser user(&objectCache, false);
			user.Run();
		});

		test.AddThread("malloc " + std::to_string(i), [&mallocCache]() {
			CacheUser user(&mallocCache, true);
			user.Run();
		});
	}

	test.Run();
}
