/*
 * Copyright 2025-2026 Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 *
 * Process-wide vref refcount cache. Wraps the kernel acquire/release/
 * create/open ioctls and owns the per-slot vref_keys so callers only
 * ever pass vref_ids.
 *
 *  - AcquireFromFd(): fstat-based identity coalesce. Same (dev, ino)
 *    used twice in this process returns the same vref_id; no duplicate
 *    kernel slot.
 *  - Acquire(id) / Release(id): bump or drop a soft refcount; only call
 *    the kernel on the first acquire or last release.
 *  - Open(id): borrows the anchor key for the dup.
 *  - AdoptCaps(caps, count): cap-transport entry point. Hands the
 *    kernel-minted (id, key) pairs from read_port_with_caps to the
 *    cache so the receiver never touches keys directly.
 */
#ifndef _STORAGE_VREF_CACHE_H
#define _STORAGE_VREF_CACHE_H

#include <OS.h>

namespace BPrivate {

class VRefCache {
public:
	static vref_id	AcquireFromFd(int fd);
	static status_t	Acquire(vref_id id);
	static status_t	Release(vref_id id);
	static int		Open(vref_id id);

	// Receiver-side cap transport: walk a port_cap_out array from
	// read_port_with_caps and install each freshly minted (id, key)
	// into the cache. If the cache already owns a slot for an id, the
	// new slot is redundant — soft_refs is bumped and the duplicate
	// kernel slot is dropped. Keys never escape the cache.
	static void		AdoptCaps(const port_cap_out* caps, size_t count);
};

} // namespace BPrivate

#endif // _STORAGE_VREF_CACHE_H
