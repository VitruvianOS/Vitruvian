/*
 * Copyright 2010, Haiku, Inc.
 * Distributed under the terms of the MIT Licence.
 */
#ifndef _B_TIME_FORMAT_H_
#define _B_TIME_FORMAT_H_

#include <DateTimeFormat.h>


class BString;

class BTimeFormat : public BDateTimeFormat {
public:
								BTimeFormat(const BTimeFormat &other);
	virtual						~BTimeFormat();

								// formatting

								// no-frills version: Simply appends the
								// formatted date to the string buffer.
								// Can fail only with B_NO_MEMORY or B_BAD_VALUE.
	virtual	status_t 			Format(bigtime_t value, BString* buffer) const;

								// TODO: ... basically, all of it!
};


#endif	// _B_TIME_FORMAT_H_
