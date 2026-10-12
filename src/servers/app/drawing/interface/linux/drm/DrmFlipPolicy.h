/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Flip-failure policy shared by DrmHWInterface and the host tests.
 */
#ifndef DRM_FLIP_POLICY_H
#define DRM_FLIP_POLICY_H

#include <errno.h>

// Only these mean the flip call itself is unsupported. EBUSY is a
// routine in-flight collision; EACCES and EPERM ride DRM master
// changes around a VT switch; both must never arm the fallback.
static inline bool
drm_flip_unsupported_errno(int error)
{
	return error == EINVAL || error == EOPNOTSUPP || error == ENOSYS;
}

// The vblank-paced fallback arms after this many unsupported flips in
// a row.
#define DRM_FLIP_FAILURE_LIMIT 8

// Update the counter for one flip failure. Transient errors and
// failures that land inside a modeset reset the run; a run that
// reaches the limit arms the vblank-paced fallback.
static inline bool
drm_flip_failure_counts(int error, bool modesetActive, unsigned* failures)
{
	if (!drm_flip_unsupported_errno(error) || modesetActive) {
		*failures = 0;
		return false;
	}
	if (*failures < DRM_FLIP_FAILURE_LIMIT)
		*failures += 1;
	return *failures >= DRM_FLIP_FAILURE_LIMIT;
}

#endif // DRM_FLIP_POLICY_H
