/*
 * Copyright 2025-2026, The Vitruvian Project. All rights reserved.
 * Distributed under the terms of the LGPL License.
 *
 * Uniform generation-guard for sem/port/area ID namespaces.
 *
 * The kernel assigns plain int32 IDs that can be recycled after deletion.
 * A stale handle held across the recycle then addresses the WRONG object.
 *
 * This header provides a template that encodes a per-slot generation counter
 * into the upper bits of the int32 ID returned to callers.  On every
 * operation the generation is extracted and validated against the table;
 * a mismatch means the handle is stale and is rejected with B_BAD_*_ID.
 *
 * Usage:
 *   static BPrivate::IdGenerationGuard<sem_id> sSemGuard;
 *   sem_id encoded = sSemGuard.Register(kernel_id);   // on create
 *   int32 kid = sSemGuard.Validate(encoded);           // on any op
 *   sSemGuard.Unregister(encoded);                      // on delete
 *
 * Layout: upper 8 bits = generation (0..255), lower 24 bits = kernel ID.
 * Valid kernel IDs are non-negative; the guard preserves that invariant
 * (generation 0 with kernel_id 0..16777215 yields values 0..16777215).
 */

#ifndef _LIBROOT2_ID_GENERATION_GUARD_H
#define _LIBROOT2_ID_GENERATION_GUARD_H

#include <OS.h>

#include <pthread.h>
#include <stdint.h>
#include <string.h>

#include <map>


namespace BPrivate {


// Maximum kernel-ID value that fits in 24 bits.
static const int32 kMaxGuardedId = 0x00FFFFFF;

// Number of generation bits (256 distinct generations before wrap).
static const int kGenerationBits = 8;
static const uint32 kGenerationMask = 0xFF000000;
static const uint32 kIdMask         = 0x00FFFFFF;


template <typename IdType>
class IdGenerationGuard {
public:
	IdGenerationGuard()
	{
		pthread_mutex_init(&fLock, NULL);
	}

	~IdGenerationGuard()
	{
		pthread_mutex_destroy(&fLock);
	}

	// Encode a generation-tracked ID from a raw kernel ID.
	// Called after a successful create_* ioctl.
	// Returns B_BAD_VALUE if kernel_id is out of range.
	IdType Register(int32 kernelId)
	{
		if (kernelId < 0 || kernelId > kMaxGuardedId)
			return (IdType)(-B_BAD_VALUE);

		pthread_mutex_lock(&fLock);

		typename SlotMap::iterator it = fSlots.find(kernelId);
		uint8 gen = 0;
		if (it != fSlots.end()) {
			// Slot existed — bump generation to invalidate old handles.
			gen = (uint8)((it->second.generation + 1) & 0xFF);
			it->second.generation = gen;
			it->second.valid = true;
		} else {
			// Fresh slot.
			Slot s;
			s.generation = 0;
			s.valid = true;
			fSlots[kernelId] = s;
			gen = 0;
		}

		IdType encoded = Encode(kernelId, gen);
		pthread_mutex_unlock(&fLock);
		return encoded;
	}

	// Validate that an encoded ID is still live.
	// Returns the raw kernel ID on success, or a negative error on failure.
	// The caller should use this kernel ID for the actual nexus ioctl.
	int32 Validate(IdType encoded) const
	{
		int32 kernelId = DecodeKernelId(encoded);
		uint8 gen      = DecodeGeneration(encoded);

		if (kernelId < 0 || kernelId > kMaxGuardedId)
			return -B_BAD_VALUE;

		pthread_mutex_lock(&fLock);

		typename SlotMap::const_iterator it = fSlots.find(kernelId);
		bool ok = (it != fSlots.end())
			&& it->second.valid
			&& it->second.generation == gen;

		pthread_mutex_unlock(&fLock);
		return ok ? kernelId : -B_BAD_VALUE;
	}

	// Mark a slot as invalid (called on delete_*).
	// The generation is bumped by the next Register() for this kernel ID,
	// so any stale encoded handle will fail Validate().
	void Unregister(IdType encoded)
	{
		int32 kernelId = DecodeKernelId(encoded);

		if (kernelId < 0 || kernelId > kMaxGuardedId)
			return;

		pthread_mutex_lock(&fLock);

		typename SlotMap::iterator it = fSlots.find(kernelId);
		if (it != fSlots.end())
			it->second.valid = false;

		pthread_mutex_unlock(&fLock);
	}

	// Static helpers for encoding/decoding without a guard instance.
	static int32 DecodeKernelId(IdType encoded)
	{
		return (int32)((uint32)encoded & kIdMask);
	}

	static uint8 DecodeGeneration(IdType encoded)
	{
		return (uint8)(((uint32)encoded & kGenerationMask) >> 24);
	}

private:
	IdType Encode(int32 kernelId, uint8 generation) const
	{
		return (IdType)(((uint32)generation << 24)
			| ((uint32)kernelId & kIdMask));
	}

	struct Slot {
		uint8  generation;  // current generation for this kernel-ID slot
		bool   valid;       // true while the object is live
	};

	typedef std::map<int32, Slot> SlotMap;
	SlotMap fSlots;

	mutable pthread_mutex_t fLock;
};


} // namespace BPrivate

#endif // _LIBROOT2_ID_GENERATION_GUARD_H
