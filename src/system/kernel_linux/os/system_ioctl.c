
#include <SupportDefs.h>

#include "syscalls.h"


status_t
system_ioctl(int fd, uint32 cmd, void *data, size_t length)
{
	UNIMPLEMENTED();
	return B_OK;
}


status_t
_kern_ioctl(int fd, uint32 cmd, void *data, size_t length)
{
	return system_ioctl(fd, cmd, data, length);
}
