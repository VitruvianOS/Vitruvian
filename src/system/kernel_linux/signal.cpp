
#include <OS.h>

#include <syscalls.h>


extern status_t
_kern_send_signal(int32 id, uint32 signal,
	const union sigval* userValue, uint32 flags)
{

}
