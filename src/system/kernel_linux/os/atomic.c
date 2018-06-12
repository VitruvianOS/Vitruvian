/* 
** Copyright (c) 2004, Bill Hayden (hayden@haydentech.com). All rights reserved.
** Copyright (c) 2018, Dario Casalinuovo
** Distributed under the terms of the MIT License.
*/

#include <SupportDefs.h>

#define __gcc_noalias__(x) (*(volatile struct { int value; } *)x)


static inline int32 atomic_exchange(vint32 *value, int32 old, int32 new)
{
	__asm__ __volatile__(
		"lock; cmpxchgl %1, %2"
			: "+a" (old)
			: "r" (new), "m" (__gcc_noalias__(value))
			: "memory");

	return old;
}


int32 atomic_add(vint32 *value, int32 addvalue)
{
      __asm__ volatile("lock; xaddl %0, %1"
        : "+r" (addvalue), "+m" (*value) // input+output
        : // No input-only
        : "memory"
      );
      return addvalue;
}


int32 atomic_or(vint32 *value, int32 orvalue)
{
	register int32 oldval;

	do {
		oldval = *value;
	} while (atomic_exchange(value, oldval, oldval | orvalue) != oldval);
     
	return oldval;
}


int32 atomic_and(vint32 *value, int32 andvalue)
{
	register int32 oldval;

	do {
		oldval = *value;
	} while (atomic_exchange(value, oldval, oldval & andvalue) != oldval);
     
	return oldval;
}


int32 atomic_get(vint32 *value)
{
	return __sync_fetch_and_add(value, 0);
}
