#include <SupportDefs.h>

// For typedefs
#include <dirent.h>			// For dirent
#include <sys/stat.h>		// For struct stat
#include <fcntl.h>			// For flock
#include <fs_info.h>		// File sytem information functions, structs, defines

// Forward Declarations
typedef struct attr_info;
typedef struct entry_ref;

// Type aliases
typedef dirent DirEntry;
typedef struct flock FileLock;
typedef struct stat Stat;
typedef uint32 StatMember;
typedef attr_info AttrInfo;
typedef int OpenFlags;			// open() flags
typedef mode_t CreationFlags;	// open() mode
typedef int SeekMode;			// lseek() mode
