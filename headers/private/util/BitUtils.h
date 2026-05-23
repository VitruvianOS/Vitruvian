/*
 * Copyright 2019-2022, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Vitruvian stub: count_set_bits via __builtin_popcount.
 */
#ifndef _UTIL_BIT_UTILS_H
#define _UTIL_BIT_UTILS_H

#include <SupportDefs.h>


static inline int
count_set_bits(uint8 value)
{
	return __builtin_popcount(value);
}


static inline int
count_set_bits(uint32 value)
{
	return __builtin_popcount(value);
}


static inline int
count_set_bits(uint64 value)
{
	return __builtin_popcountll(value);
}


#endif	// _UTIL_BIT_UTILS_H
