/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "UnreadParser.h"

#include <cstdlib>
#include <cstring>


int32
ParseUnreadCount(const char* title)
{
	if (title == NULL)
		return 0;

	const char* scan = title;
	while ((scan = strchr(scan, '(')) != NULL) {
		scan++;
		if (*scan < '0' || *scan > '9')
			continue;

		char* end = NULL;
		long value = strtol(scan, &end, 10);
		if (end == NULL || *end != ')')
			continue;
		if (value < 0)
			value = 0;
		if (value > 9999)
			value = 9999;
		return (int32)value;
	}
	return 0;
}
