/*
 * Copyright 2010, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Oliver Tappe <zooey@hirschkaefer.de>
 */

#include <TimeFormat.h>


// copy constructor
BTimeFormat::BTimeFormat(const BTimeFormat &other)
	: BDateTimeFormat(other)
{
}

// destructor
BTimeFormat::~BTimeFormat()
{
}

// Format
status_t
BTimeFormat::Format(bigtime_t value, BString* buffer) const
{
	return B_ERROR;
}
