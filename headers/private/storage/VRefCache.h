/*
 * Copyright 2025-2026 Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 *
 * Process-wide vref slot+key cache. Each Acquire mints a fresh ticket;
 * Release(id, ticket) tears down only that slot. The kernel slot is
 * dropped only when the last ticket is released, so an over-release
 * by one holder cannot invalidate refs held by another.
 */
#ifndef _STORAGE_VREF_CACHE_H
#define _STORAGE_VREF_CACHE_H

#include <OS.h>


namespace BPrivate {


typedef uint64 vref_ticket;
static const vref_ticket B_INVALID_VREF_TICKET = 0;


struct vref_handle {
	vref_id		id;
	vref_ticket	ticket;
};


class VRefCache {
public:
	static vref_handle	AcquireFromFd(int fd);
	static vref_ticket	Acquire(vref_id id);
	static status_t		Release(vref_id id, vref_ticket ticket);
	static int			Open(vref_id id);

	// Install caps from read_port_with_caps. Fills `tickets` (if
	// non-NULL) with one entry per cap; caller owns those tickets.
	static void			AdoptCaps(const port_cap_out* caps, size_t count,
							vref_ticket* tickets = NULL);
};


} // namespace BPrivate

#endif // _STORAGE_VREF_CACHE_H
