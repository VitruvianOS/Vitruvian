#include <SupportDefs.h>

void _kern_clear_caches(void *address, size_t length,
	uint32 flags)
{

}


status_t		_kern_remove_dir(int fd, const char *path)
{
	return B_ERROR;
}


status_t
_kern_read_fs_info(dev_t device, struct fs_info *info)
{
}
