/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SUPPORT_BRANCH_HINTS_H
#define _SUPPORT_BRANCH_HINTS_H


#ifndef likely
#	define likely(x)	__builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#	define unlikely(x)	__builtin_expect(!!(x), 0)
#endif


#endif	// _SUPPORT_BRANCH_HINTS_H
