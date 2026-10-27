/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef _PROC_READ_H
#define _PROC_READ_H

// /proc and /sys readers shared by the kernel-kit info calls.

#include <OS.h>

#include <sys/types.h>


struct proc_stat_info {
	pid_t		ppid;
	pid_t		pgrp;
	pid_t		session;
	uint64		utimeTicks;
	uint64		stimeTicks;
	uint64		startTimeTicks;
};


struct proc_status_info {
	char		name[B_OS_NAME_LENGTH];	// control chars dropped
	char		state;				// raw first char, 0 if absent
	pid_t		tgid;
	pid_t		ppid;
	uid_t		realUid;
	uid_t		effectiveUid;
	gid_t		realGid;
	gid_t		effectiveGid;
	int32		threadCount;
};


struct proc_map_line {
	unsigned long long	start;
	unsigned long long	end;
	unsigned long long	offset;
	unsigned long long	inode;
	unsigned int		devMajor;
	unsigned int		devMinor;
	char				perms[8];
	// Into the caller's line, trimmed; not NUL-terminated, use pathLength.
	const char*			path;
	size_t				pathLength;
};


status_t proc_errno_to_status(int error);


// Pinned /proc/<pid>: files open relative to it, so a reused pid can never
// leak another team's data.
struct proc_dir {
	int		dirFd;
};


bool proc_dir_open_pid(struct proc_dir* dir, pid_t pid);
void proc_dir_close(struct proc_dir* dir);

int proc_dir_open_file(const struct proc_dir* dir, const char* name);

// relative may name a subdirectory, e.g. "<tid>/comm" under a task fd.
int proc_openat_file(int parentFd, const char* relative);


// Whole-file read, always NUL-terminated; bytes stored or -1.
ssize_t proc_read_all(int fd, char* buffer, size_t size);

// Seeking a seq_file regenerates it, so maps/smaps are read whole.
bool proc_slurp(int fd, char** buffer, size_t* length, size_t maxBytes);


// Parses after the last ')': a thread name may contain parentheses.
bool proc_parse_stat(const char* text, struct proc_stat_info* info);

bool proc_parse_status(const char* text, struct proc_status_info* info);

// "start-end perms offset dev inode path"; false if not a mapping line.
bool proc_parse_map_header(const char* line, struct proc_map_line* out);

bool proc_read_sysfs_uint(const char* path, unsigned long* value);


// Lines longer than kMaxLine - 1 are truncated.
class ProcLineReader {
public:
							ProcLineReader();
							~ProcLineReader();

			bool			Open(const struct proc_dir* dir,
								const char* name);
			bool			OpenPath(const char* path);
			bool			OpenFd(int fd);	// takes ownership
			void			Close();

			// Without the newline; NULL at end or on error.
			const char*		Next();

	enum { kMaxLine = 1024 };

private:
			bool			_Refill();

			int				fFd;
			char			fBuffer[8192];
			size_t			fOffset;
			size_t			fUsed;
			char			fLine[kMaxLine];
			bool			fEof;
};


// Constant after boot; computed on first use, no lock.
long proc_clk_tck();
long proc_page_size();

// CLOCK_REALTIME at boot in microseconds, 0 when unavailable. Cached per
// thread; proc_refresh_boot_time() rereads it.
void proc_refresh_boot_time();
uint64 proc_boot_time_micros();


// get_next_* cookies: 0 starts, a positive value is a handle to the walk's
// state. A walk that is not drained leaks its handle.
int32 proc_enum_alloc(void* state);
void* proc_enum_state(int32 cookie);
void* proc_enum_release(int32 cookie);

void proc_enum_reset_after_fork();


// One getdents64 per batch, no allocation.
class ProcDirBatch {
public:
							ProcDirBatch();
							~ProcDirBatch();

			bool			Open(const char* path);
			void			Close();

			// Next positive numeric entry; false at end.
			bool			NextPid(pid_t* pid);

			int				Fd() const { return fFd; }

private:
			bool			_Refill();

	enum { kBufferSize = 4096 };

			int				fFd;
			char			fBuffer[kBufferSize];
			size_t			fOffset;
			size_t			fUsed;
};


#endif // _PROC_READ_H
