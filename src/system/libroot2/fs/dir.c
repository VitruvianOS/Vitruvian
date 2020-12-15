// TODO fix that
typedef struct __DIR {
	int				fd;
	short			next_entry;
	unsigned short	entries_left;
	long			seek_position;
	long			current_position;
} DIR;

extern DIR*
__create_dir_struct(int fd)
{
	// allocate the memory for the DIR structure 

	DIR* dir = (DIR*)malloc(sizeof(DIR));
	if (dir == NULL) {
		//__set_errno(B_NO_MEMORY);
		return NULL;
	}

	dir->fd = fd;
	dir->entries_left = 0;
	dir->seek_position = 0;
	dir->current_position = 0;

	return dir;
}
