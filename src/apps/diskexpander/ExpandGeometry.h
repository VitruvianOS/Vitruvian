/*
 * Copyright 2026, the Vitruvian project.
 * Distributed under the terms of the MIT License.
 */
#ifndef _EXPAND_GEOMETRY_H
#define _EXPAND_GEOMETRY_H

// Pure byte-range arithmetic behind the ExpandVolume probe. No Be API, no
// I/O: this is the fake-allocation unit path (ExpandGeometryTest feeds it
// synthetic partition tables). The window code converts BPartition values
// into these structs and interprets the result.

#include <stdint.h>

namespace ExpandVolumeGeometry {


struct SpaceRange {
	int64_t		offset;
	int64_t		size;
};


// Is [offset, offset+size) a plausible partitionable range?
inline bool
IsValidSpace(const SpaceRange& space)
{
	return space.size > 0 && space.offset >= 0
		&& space.offset + space.size > space.offset;
}


// The claimable gap for a grow-in-place resize is the partitionable range
// that starts exactly where the partition ends. A range further away (or
// only before the partition) would need a move first and is deliberately
// NOT claimable here: stretching allocations from non-adjacent free space
// is how tables get corrupted. Returns true and the largest such range.
inline bool
FindAdjacentTailGap(int64_t partitionOffset, int64_t partitionSize,
	const SpaceRange* spaces, int32_t count, SpaceRange* outGap)
{
	if (partitionOffset < 0 || partitionSize <= 0 || spaces == nullptr
		|| count <= 0 || outGap == nullptr) {
		return false;
	}

	int64_t partitionEnd = partitionOffset + partitionSize;
	if (partitionEnd < partitionOffset)
		return false;

	bool found = false;
	SpaceRange best = { 0, 0 };
	for (int32_t i = 0; i < count; i++) {
		if (!IsValidSpace(spaces[i]))
			continue;
		if (spaces[i].offset != partitionEnd)
			continue;
		if (!found || spaces[i].size > best.size) {
			best = spaces[i];
			found = true;
		}
	}

	if (!found)
		return false;

	*outGap = best;
	return true;
}


// Clamp a requested new partition size into [current, current+gap],
// keeping the result block-aligned relative to the partition offset (the
// partition must stay on block boundaries or the table write is invalid).
// Returns false if nothing valid remains (gap empty, blockSize bogus, or
// the request is below the current size and shrinking is not on offer).
inline bool
ClampTargetSize(int64_t partitionOffset, int64_t currentSize, int64_t gapSize,
	int64_t blockSize, int64_t requestedSize, int64_t* outSize)
{
	if (partitionOffset < 0 || currentSize <= 0 || gapSize < 0
		|| blockSize <= 0 || outSize == nullptr) {
		return false;
	}
	if (requestedSize < currentSize)
		return false;
	if (gapSize == 0)
		return false;

	int64_t maxSize = currentSize + gapSize;
	if (maxSize < currentSize)
		return false;	// overflow: refuse rather than wrap

	int64_t size = requestedSize;
	if (size > maxSize)
		size = maxSize;

	// Snap down to a multiple of blockSize measured from the offset.
	int64_t position = partitionOffset + size;
	int64_t aligned = partitionOffset
		+ (position - partitionOffset) / blockSize * blockSize;
	if (aligned < partitionOffset + currentSize)
		return false;	// alignment ate below the current size

	*outSize = aligned - partitionOffset;
	return true;
}


// The "Use all free" preset: current size plus the whole adjacent gap,
// block-aligned down. Returns false when there is nothing to claim.
inline bool
ProposeAllFree(int64_t partitionOffset, int64_t currentSize, int64_t gapSize,
	int64_t blockSize, int64_t* outSize)
{
	return ClampTargetSize(partitionOffset, currentSize, gapSize, blockSize,
		currentSize + gapSize, outSize);
}


}	// namespace ExpandVolumeGeometry

#endif	// _EXPAND_GEOMETRY_H
