#include <syscalls.h>


status_t
_kern_mutex_lock(int32* mutex, const char* name,
	uint32 flags, bigtime_t timeout)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_mutex_unlock(int32* mutex, uint32 flags)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_mutex_switch_lock(int32* fromMutex, int32* toMutex,
	const char* name, uint32 flags, bigtime_t timeout)
{
	UNIMPLEMENTED();
	return B_ERROR;
}
