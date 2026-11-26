/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _LIBROOT_ERRNO_STATUS_H
#define _LIBROOT_ERRNO_STATUS_H

#include <errno.h>

#include <Errors.h>
#include <SupportDefs.h>


// ESTALE: the server-side object is gone (deleted or renamed elsewhere);
// the storage API reports that as B_ENTRY_NOT_FOUND.
static inline status_t
vos_status_from_errno(int err)
{
	if (err == ESTALE)
		return B_ENTRY_NOT_FOUND;
	return -err;
}


#endif // _LIBROOT_ERRNO_STATUS_H
