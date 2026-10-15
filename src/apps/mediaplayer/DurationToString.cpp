/*
 * Copyright 2010, Stephan Aßmus <superstippi@gmx.de>.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "DurationToString.h"

#include <stdio.h>


// deciseconds, so clips under 10 s can show a tenth; sign is the
// caller's concern (negative means "to finish" in the duration view).
void
duration_to_string(int32 deciseconds, char* string, size_t stringSize)
{
	bool negative = deciseconds < 0;
	if (negative)
		deciseconds = -deciseconds;

	int32 tenths = deciseconds % 10;
	int32 seconds = deciseconds / 10;
	int32 hours = seconds / 3600;
	seconds -= hours * 3600;
	int32 minutes = seconds / 60;
	seconds = seconds % 60;

	if (deciseconds < 100) {
		snprintf(string, stringSize, "%s%" B_PRId32 ":%02" B_PRId32 ".%" B_PRId32,
			negative ? "-" : "", minutes, seconds, tenths);
	} else if (hours > 0) {
		snprintf(string, stringSize, "%s%" B_PRId32 ":%02" B_PRId32 ":%02"
			B_PRId32, negative ? "-" : "", hours, minutes, seconds);
	} else {
		snprintf(string, stringSize, "%s%" B_PRId32 ":%02" B_PRId32,
			negative ? "-" : "", minutes, seconds);
	}
}
