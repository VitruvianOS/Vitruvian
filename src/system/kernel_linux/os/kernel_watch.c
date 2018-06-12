#include <OS.h>

status_t		_kern_stop_notifying(port_id port, uint32 token)
{
	UNIMPLEMENTED();
	return B_ERROR;
}

status_t		_kern_start_watching(dev_t device, ino_t node, uint32 flags,
						port_id port, uint32 token)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t		_kern_stop_watching(dev_t device, ino_t node, port_id port,
						uint32 token)
{
	UNIMPLEMENTED();
	return B_ERROR;
}
