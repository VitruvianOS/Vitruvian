
#include <fs_index.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>

#include <dirent_private.h>
#include <errno_private.h>
#include <syscalls.h>
#include <syscall_utils.h>


int
_kern_create_index(dev_t device, const char *name, uint32 type, uint32 flags)
{
	return 0;
}


int
_kern_remove_index(dev_t device, const char *name)
{
	return 0;	
}


int
_kern_stat_index(dev_t device, const char *name, struct index_info *indexInfo)
{
	return 0;
}


int
_kern_open_index_dir(dev_t device)
{
	return NULL;
}
