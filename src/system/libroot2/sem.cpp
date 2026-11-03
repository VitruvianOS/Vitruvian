/*
 *  Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

// The count is the low half of the control word, and the futex word.

#include <OS.h>

#include <atomic>
#include <string.h>
#include <sched.h>
#include <sys/mman.h>
#include <unistd.h>

#include <libroot/futex.h>

#include "Team.h"

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
	"futex word is the low half of the control word");


namespace {

const int32 kChunkShift = 10;
const int32 kChunkSlots = 1 << kChunkShift;
const int32 kMaxChunks = 64;
const uint32 kGenerationMask = 0x7fff;
const int32 kDeadCount = INT32_MIN;


struct SemSlot {
	std::atomic<uint64_t>	ctl;
	std::atomic<int32>		waiters;
	std::atomic<int32>		holder;
	int32					nextFree;
	char					name[B_OS_NAME_LENGTH];
};


std::atomic<SemSlot*> sChunks[kMaxChunks];
std::atomic_flag sLock = ATOMIC_FLAG_INIT;
int32 sFreeHead = -1;
std::atomic<int32> sUsed(0);


inline void
lock_slots()
{
	while (sLock.test_and_set(std::memory_order_acquire))
		sched_yield();
}


inline void
unlock_slots()
{
	sLock.clear(std::memory_order_release);
}


inline uint32
state_of(uint64_t ctl)
{
	return (uint32)(ctl >> 32);
}


inline int32
count_of(uint64_t ctl)
{
	return (int32)(uint32)ctl;
}


inline uint64_t
make_ctl(uint32 state, int32 count)
{
	return ((uint64_t)state << 32) | (uint32)count;
}


inline volatile int32*
futex_word(SemSlot* slot)
{
	return (volatile int32*)&slot->ctl;
}


inline SemSlot*
slot_at(int32 index)
{
	SemSlot* chunk = sChunks[index >> kChunkShift].load(
		std::memory_order_acquire);
	return chunk != NULL ? &chunk[index & (kChunkSlots - 1)] : NULL;
}


// Returns the slot only if the id's generation is the live one.
SemSlot*
lookup(sem_id id, uint32* state)
{
	if (id <= 0)
		return NULL;

	uint32 generation = (uint32)id >> 16;
	int32 index = id & 0xffff;
	if (generation == 0 || (index >> kChunkShift) >= kMaxChunks)
		return NULL;

	SemSlot* slot = slot_at(index);
	if (slot == NULL)
		return NULL;

	*state = (generation << 1) | 1;
	if (state_of(slot->ctl.load(std::memory_order_acquire)) != *state)
		return NULL;
	return slot;
}


int32
count_for_user(SemSlot* slot, uint64_t ctl)
{
	int32 count = count_of(ctl);
	if (count > 0)
		return count;
	return -slot->waiters.load(std::memory_order_relaxed);
}


struct WaiterRef {
	SemSlot*	slot;
	bool		registered;

	WaiterRef(SemSlot* slot) : slot(slot), registered(false) {}
	~WaiterRef()
	{
		if (registered)
			slot->waiters.fetch_sub(1);
	}
	void Register()
	{
		registered = true;
		slot->waiters.fetch_add(1);
	}
};

}	// namespace


void
BKernelPrivate::ReinitSemsAfterFork()
{
	sLock.clear();
	int32 used = sUsed.load();
	for (int32 i = 0; i < used; i++) {
		SemSlot* slot = slot_at(i);
		if (slot != NULL)
			slot->waiters.store(0);
	}
}


sem_id
create_sem(int32 count, const char* name)
{
	if (count < 0)
		return B_BAD_VALUE;

	lock_slots();
	int32 index = sFreeHead;
	if (index >= 0) {
		sFreeHead = slot_at(index)->nextFree;
	} else {
		index = sUsed.load();
		if (index >= kMaxChunks * kChunkSlots) {
			unlock_slots();
			return B_NO_MORE_SEMS;
		}
		if ((index & (kChunkSlots - 1)) == 0) {
			void* memory = mmap(NULL, kChunkSlots * sizeof(SemSlot),
				PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			if (memory == MAP_FAILED) {
				unlock_slots();
				return B_NO_MEMORY;
			}
			sChunks[index >> kChunkShift].store((SemSlot*)memory,
				std::memory_order_release);
		}
		sUsed.store(index + 1);
	}
	unlock_slots();

	SemSlot* slot = slot_at(index);
	uint32 generation = (state_of(slot->ctl.load()) >> 1) + 1;
	if ((generation & kGenerationMask) == 0)
		generation = 1;
	generation &= kGenerationMask;

	// Keep waiters, the deleted sem's waiters still decrement it.
	strlcpy(slot->name, name != NULL ? name : "", B_OS_NAME_LENGTH);
	slot->holder.store(-1);
	slot->ctl.store(make_ctl((generation << 1) | 1, count),
		std::memory_order_release);
	return (sem_id)((generation << 16) | (uint32)index);
}


status_t
delete_sem(sem_id id)
{
	uint32 state;
	SemSlot* slot = lookup(id, &state);
	if (slot == NULL)
		return B_BAD_SEM_ID;

	uint64_t ctl = slot->ctl.load();
	do {
		if (state_of(ctl) != state)
			return B_BAD_SEM_ID;
	} while (!slot->ctl.compare_exchange_weak(ctl,
		make_ctl(state & ~1u, kDeadCount)));

	futex_wake_all(futex_word(slot));

	lock_slots();
	slot->nextFree = sFreeHead;
	sFreeHead = id & 0xffff;
	unlock_slots();
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
	uint32 state;
	SemSlot* slot = lookup(id, &state);
	if (slot == NULL)
		return B_BAD_SEM_ID;

	if (count < 1)
		return B_BAD_VALUE;

	bool hasTimeout = (flags & (B_RELATIVE_TIMEOUT | B_ABSOLUTE_TIMEOUT)) != 0
		&& timeout != B_INFINITE_TIMEOUT;
	bigtime_t deadline = 0;
	if (hasTimeout && timeout > 0) {
		deadline = (flags & B_ABSOLUTE_TIMEOUT) != 0
			? timeout : system_time() + timeout;
		if (deadline <= 0)
			deadline = 1;
	}

	WaiterRef waiter(slot);
	for (;;) {
		uint64_t ctl = slot->ctl.load(std::memory_order_acquire);
		if (state_of(ctl) != state)
			return B_BAD_SEM_ID;

		int32 available = count_of(ctl);
		if (available >= count) {
			if (slot->ctl.compare_exchange_weak(ctl,
					make_ctl(state, available - count))) {
				slot->holder.store(find_thread(NULL),
					std::memory_order_relaxed);
				return B_OK;
			}
			continue;
		}

		if (hasTimeout && deadline == 0)
			return (flags & B_ABSOLUTE_TIMEOUT) != 0 ? B_TIMED_OUT : B_WOULD_BLOCK;

		// Count ourselves, then look once more before sleeping.
		if (!waiter.registered) {
			waiter.Register();
			continue;
		}

		bigtime_t left = 0;
		if (deadline != 0) {
			left = futex_time_left(deadline);
			if (left < 0)
				return B_TIMED_OUT;
		}

		status_t status = futex_wait(futex_word(slot), available, left);
		if (status == B_INTERRUPTED && (flags & B_CAN_INTERRUPT) != 0)
			return B_INTERRUPTED;
		if (status == B_TIMED_OUT) {
			if (deadline != 0)
				return B_TIMED_OUT;
		} else if (futex_wait_failed(status))
			return B_ERROR;
	}
}


status_t
release_sem(sem_id id)
{
	return release_sem_etc(id, 1, 0);
}


status_t
release_sem_etc(sem_id id, int32 count, uint32 flags)
{
	uint32 state;
	SemSlot* slot = lookup(id, &state);
	if (slot == NULL)
		return B_BAD_SEM_ID;

	if (count < 1)
		return B_BAD_VALUE;

	uint64_t ctl = slot->ctl.load(std::memory_order_acquire);
	for (;;) {
		if (state_of(ctl) != state)
			return B_BAD_SEM_ID;

		int32 current = count_of(ctl);
		int32 add = count;
		if ((flags & B_RELEASE_ALL) != 0) {
			add = slot->waiters.load();
			if (add < 1)
				return B_OK;
		}
		int32 updated = add > INT32_MAX - current ? INT32_MAX : current + add;
		if (slot->ctl.compare_exchange_weak(ctl, make_ctl(state, updated)))
			break;
	}

	if (slot->waiters.load() > 0)
		futex_wake_all(futex_word(slot));
	return B_OK;
}


status_t
get_sem_count(sem_id id, int32* threadCount)
{
	uint32 state;
	SemSlot* slot = lookup(id, &state);
	if (slot == NULL)
		return B_BAD_SEM_ID;

	if (threadCount == NULL)
		return B_BAD_VALUE;

	uint64_t ctl = slot->ctl.load();
	if (state_of(ctl) != state)
		return B_BAD_SEM_ID;
	*threadCount = count_for_user(slot, ctl);
	return B_OK;
}


static status_t
fill_info(SemSlot* slot, sem_id id, uint32 state, struct sem_info* info)
{
	uint64_t ctl = slot->ctl.load();
	if (state_of(ctl) != state)
		return B_BAD_SEM_ID;

	info->sem = id;
	info->team = getpid();
	strlcpy(info->name, slot->name, B_OS_NAME_LENGTH);
	info->count = count_for_user(slot, ctl);
	info->latest_holder = slot->holder.load(std::memory_order_relaxed);

	// The slot may have been recycled while the name was copied.
	return state_of(slot->ctl.load()) == state ? B_OK : B_BAD_SEM_ID;
}


status_t
_get_sem_info(sem_id id, struct sem_info* info, size_t infoSize)
{
	uint32 state;
	SemSlot* slot = lookup(id, &state);
	if (slot == NULL)
		return B_BAD_SEM_ID;

	if (info == NULL || infoSize != sizeof(sem_info))
		return B_BAD_VALUE;

	return fill_info(slot, id, state, info);
}


status_t
_get_next_sem_info(team_id id, int32* cookie, struct sem_info* info, size_t size)
{
	if (cookie == NULL || info == NULL || size != sizeof(sem_info))
		return B_BAD_VALUE;

	if (id != 0 && id != getpid())
		return B_BAD_TEAM_ID;

	int32 used = sUsed.load();
	for (int32 index = *cookie; index >= 0 && index < used; index++) {
		SemSlot* slot = slot_at(index);
		if (slot == NULL)
			break;

		uint64_t ctl = slot->ctl.load();
		if ((state_of(ctl) & 1) == 0)
			continue;

		sem_id sem = (sem_id)(((state_of(ctl) >> 1) << 16) | (uint32)index);
		if (fill_info(slot, sem, state_of(ctl), info) == B_OK) {
			*cookie = index + 1;
			return B_OK;
		}
	}
	*cookie = used;
	return B_BAD_VALUE;
}


status_t
set_sem_owner(sem_id id, team_id team)
{
	UNIMPLEMENTED();
	// This is not really used beyond device drivers, deprecated.
	return B_UNSUPPORTED;
}
