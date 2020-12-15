/*
 * Copyright 2018, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <TLS.h>

#include <pthread.h>


extern int32
tls_allocate()
{
	pthread_key_t key = 0;
	return pthread_key_create(&key, NULL);
}


extern void*
tls_get(int32 index)
{
	return pthread_getspecific(index);
}


extern void
tls_set(int32 index, void* data)
{
	pthread_setspecific(index, data);
}
