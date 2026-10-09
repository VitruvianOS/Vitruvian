/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// Host-side check of vos_status_strerror / strerror from libroot2.
// Builds the table and lookup on their own; does not need V\OS runtime.

#include <Errors.h>
#include <errno.h>

#include <stdio.h>
#include <string.h>


extern "C" char* strerror(int errnum);


static int sFailures;


static void
check(int errnum, const char* label, const char* expected)
{
	const char* got = strerror(errnum);
	int ok = (expected == NULL) ? (got != NULL) : (strcmp(got, expected) == 0);
	printf("%-28s (%4d): %-40s %s\n",
		label, errnum, got != NULL ? got : "(null)",
		ok ? "OK" : "FAIL");
	if (!ok) {
		sFailures++;
		if (expected != NULL)
			printf("    expected: %s\n", expected);
	}
}


int
main()
{
	// Required sample set from the job brief.
	check(-EINVAL, "-EINVAL / B_BAD_VALUE", "Bad value");
	check(B_NO_MEMORY, "B_NO_MEMORY", "Out of memory");
	check(B_ERROR, "B_ERROR", "General error");
	check(B_ENTRY_NOT_FOUND, "B_ENTRY_NOT_FOUND", "Entry not found");
	check(B_MEDIA_BAD_NODE, "B_MEDIA_BAD_NODE", "Bad media node");
	check(B_UNSUPPORTED, "B_UNSUPPORTED (storage)", "Operation not supported");
	check(EINVAL, "EINVAL (positive)", "Invalid argument");
	check(0, "0", "Success");
	check(123456, "123456 (unknown)", "Unknown error 123456");

	// Extra: another -errno alias and a native app code.
	check(B_TIMED_OUT, "B_TIMED_OUT", NULL);
	check(B_NAME_NOT_FOUND, "B_NAME_NOT_FOUND", "Name not found");
	check(B_LAUNCH_FAILED, "B_LAUNCH_FAILED", "Launch failed");
	check(-22, "-22 not via name", "Bad value");

	printf("\n%d failure(s)\n", sFailures);
	return sFailures == 0 ? 0 : 1;
}
