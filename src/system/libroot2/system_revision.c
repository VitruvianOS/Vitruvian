/*
 * Copyright 2025, Vitruvian OS Authors. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */


#include <system_revision.h>

#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>


// VOS_REVISION is injected by CMake from the latest "rev-N" git tag.
// Falls back to "dev" when building outside CI.
#ifndef VOS_REVISION
#  define VOS_REVISION "dev"
#endif


// Lazy-initialized revision string: VOS_REVISION at build time, or the
// Linux kernel release string as a last-resort runtime fallback.
static char sVosRevision[SYSTEM_REVISION_LENGTH];
static int sVosRevisionInitialized = 0;

const char*
__get_vos_revision(void)
{
	if (!sVosRevisionInitialized) {
		struct utsname buf;
		int hasUname = (uname(&buf) == 0);

		if (VOS_REVISION[0] != '\0') {
			// Always combine the git rev tag with the kernel release string.
			if (hasUname)
				snprintf(sVosRevision, SYSTEM_REVISION_LENGTH,
					"%s (%s)", VOS_REVISION, buf.release);
			else
				strncpy(sVosRevision, VOS_REVISION, SYSTEM_REVISION_LENGTH - 1);
		} else {
			// No compile-time tag at all: fall back to bare kernel release.
			if (hasUname)
				strncpy(sVosRevision, buf.release, SYSTEM_REVISION_LENGTH - 1);
		}
		sVosRevision[SYSTEM_REVISION_LENGTH - 1] = '\0';
		sVosRevisionInitialized = 1;
	}
	return sVosRevision;
}
