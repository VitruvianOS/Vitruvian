/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "proc_read.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <map>


status_t
proc_errno_to_status(int error)
{
	switch (error) {
		case EACCES:
		case EPERM:
			return B_PERMISSION_DENIED;
		case ENOENT:
		case ESRCH:
			return B_ENTRY_NOT_FOUND;
		case EINVAL:
			return B_BAD_VALUE;
		default:
			return B_ERROR;
	}
}


bool
proc_dir_open_pid(struct proc_dir* dir, pid_t pid)
{
	if (dir == NULL || pid <= 0)
		return false;

	char path[64];
	snprintf(path, sizeof(path), "/proc/%d", (int)pid);

	dir->dirFd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	return dir->dirFd >= 0;
}


void
proc_dir_close(struct proc_dir* dir)
{
	if (dir == NULL || dir->dirFd < 0)
		return;

	close(dir->dirFd);
	dir->dirFd = -1;
}


int
proc_dir_open_file(const struct proc_dir* dir, const char* name)
{
	if (dir == NULL || dir->dirFd < 0 || name == NULL)
		return -1;

	return openat(dir->dirFd, name, O_RDONLY | O_CLOEXEC);
}


int
proc_openat_file(int parentFd, const char* relative)
{
	if (parentFd < 0 || relative == NULL)
		return -1;

	return openat(parentFd, relative, O_RDONLY | O_CLOEXEC);
}


ssize_t
proc_read_all(int fd, char* buffer, size_t size)
{
	if (fd < 0 || buffer == NULL || size == 0) {
		errno = EINVAL;
		return -1;
	}

	size_t used = 0;
	while (used < size - 1) {
		ssize_t count = read(fd, buffer + used, size - 1 - used);
		if (count < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (count == 0)
			break;
		used += (size_t)count;
	}

	buffer[used] = '\0';
	return (ssize_t)used;
}


bool
proc_slurp(int fd, char** buffer, size_t* length, size_t maxBytes)
{
	if (fd < 0 || buffer == NULL || length == NULL || maxBytes == 0) {
		errno = EINVAL;
		return false;
	}

	size_t capacity = 256 * 1024;
	if (capacity > maxBytes)
		capacity = maxBytes;

	char* data = (char*)malloc(capacity);
	if (data == NULL)
		return false;

	size_t used = 0;
	for (;;) {
		if (used == capacity) {
			if (capacity >= maxBytes) {
				free(data);
				errno = EOVERFLOW;
				return false;
			}
			capacity *= 2;
			if (capacity > maxBytes)
				capacity = maxBytes;

			char* grown = (char*)realloc(data, capacity);
			if (grown == NULL) {
				free(data);
				return false;
			}
			data = grown;
		}

		ssize_t count = read(fd, data + used, capacity - used);
		if (count < 0) {
			if (errno == EINTR)
				continue;
			free(data);
			return false;
		}
		if (count == 0)
			break;
		used += (size_t)count;
	}

	*buffer = data;
	*length = used;
	return true;
}


static const char*
_SkipBlanks(const char* p, const char* end)
{
	while (p < end && (*p == ' ' || *p == '\t'))
		p++;
	return p;
}


static unsigned long
_ParseULong(const char* p, const char* end)
{
	p = _SkipBlanks(p, end);
	if (p >= end)
		return 0;

	// The surrounding buffer is NUL-terminated and a status line ends in
	// '\n', neither of which is a digit, so strtoul stops on its own.
	return strtoul(p, NULL, 10);
}


bool
proc_parse_stat(const char* text, struct proc_stat_info* info)
{
	if (text == NULL || info == NULL)
		return false;

	memset(info, 0, sizeof(*info));

	// The thread name (field 2) may itself contain spaces and parentheses;
	// only the last ')' reliably ends it.
	const char* paren = strrchr(text, ')');
	if (paren == NULL)
		return false;

	const char* p = paren + 1;

	// Field 3 is the single-character state; skip it.
	if (*p == '\0')
		return false;
	while (*p != '\0' && *p != ' ')
		p++;
	while (*p == ' ')
		p++;

	int field = 4;
	while (*p != '\0' && field <= 22) {
		char* stop = NULL;
		unsigned long long value = strtoull(p, &stop, 10);
		if (stop == p)
			break;

		switch (field) {
			case 4:
				info->ppid = (pid_t)value;
				break;
			case 5:
				info->pgrp = (pid_t)value;
				break;
			case 6:
				info->session = (pid_t)value;
				break;
			case 14:
				info->utimeTicks = value;
				break;
			case 15:
				info->stimeTicks = value;
				break;
			case 22:
				info->startTimeTicks = value;
				break;
			default:
				break;
		}

		if (field == 22)
			break;

		p = stop;
		while (*p == ' ')
			p++;
		field++;
	}

	return true;
}


static void
_CopyStatusName(const char* p, size_t length, char* name)
{
	p = _SkipBlanks(p, p + length);
	size_t used = 0;
	while (used < B_OS_NAME_LENGTH - 1) {
		unsigned char c = (unsigned char)*p;
		if (c == '\0' || c == '\n' || c == '\r' || c < 0x20 || c == 0x7f)
			break;
		name[used++] = (char)c;
		p++;
	}
	name[used] = '\0';
}


bool
proc_parse_status(const char* text, struct proc_status_info* info)
{
	if (text == NULL || info == NULL)
		return false;

	memset(info, 0, sizeof(*info));

	const char* line = text;
	while (*line != '\0') {
		const char* end = strchr(line, '\n');
		size_t length = (end != NULL) ? (size_t)(end - line)
			: strlen(line);
		const char* value = line + length;
		size_t valueLength = 0;

		// Prefixes match only at the start of a real line, so a value that
		// happens to contain "Tgid:" can never be mistaken for the field.
		if (strncmp(line, "Name:", 5) == 0 && length > 5) {
			_CopyStatusName(line + 5, length - 5, info->name);
		} else if (strncmp(line, "State:", 6) == 0 && length > 6) {
			value = _SkipBlanks(line + 6, line + length);
			valueLength = (size_t)((line + length) - value);
			if (valueLength > 0)
				info->state = *value;
		} else if (strncmp(line, "Tgid:", 5) == 0 && length > 5) {
			info->tgid = (pid_t)_ParseULong(line + 5, line + length);
		} else if (strncmp(line, "PPid:", 5) == 0 && length > 5) {
			info->ppid = (pid_t)_ParseULong(line + 5, line + length);
		} else if (strncmp(line, "Uid:", 4) == 0 && length > 4) {
			const char* endLine = line + length;
			const char* p = line + 4;
			info->realUid = (uid_t)_ParseULong(p, endLine);
			p = _SkipBlanks(p, endLine);
			while (p < endLine && *p >= '0' && *p <= '9')
				p++;
			info->effectiveUid = (uid_t)_ParseULong(p, endLine);
		} else if (strncmp(line, "Gid:", 4) == 0 && length > 4) {
			const char* endLine = line + length;
			const char* p = line + 4;
			info->realGid = (gid_t)_ParseULong(p, endLine);
			p = _SkipBlanks(p, endLine);
			while (p < endLine && *p >= '0' && *p <= '9')
				p++;
			info->effectiveGid = (gid_t)_ParseULong(p, endLine);
		} else if (strncmp(line, "Threads:", 8) == 0 && length > 8) {
			info->threadCount = (int32)_ParseULong(line + 8,
				line + length);
		}

		if (end == NULL)
			break;
		line = end + 1;
	}

	return true;
}


// Manual field scanners: strtoull is measurably slower than a digit loop on
// the hot maps path (a full team enumeration parses every line).
static const char*
_ScanHex(const char* p, unsigned long long* value)
{
	unsigned long long result = 0;
	const char* start = p;
	while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')
			|| (*p >= 'A' && *p <= 'F')) {
		unsigned digit;
		if (*p <= '9')
			digit = (unsigned)(*p - '0');
		else if (*p <= 'F')
			digit = (unsigned)(*p - 'A' + 10);
		else
			digit = (unsigned)(*p - 'a' + 10);
		result = (result << 4) + digit;
		p++;
	}

	if (p == start)
		return NULL;

	*value = result;
	return p;
}


static const char*
_ScanDecimal(const char* p, unsigned long long* value)
{
	unsigned long long result = 0;
	const char* start = p;
	while (*p >= '0' && *p <= '9') {
		result = result * 10 + (unsigned)(*p - '0');
		p++;
	}

	if (p == start)
		return NULL;

	*value = result;
	return p;
}


bool
proc_parse_map_header(const char* line, struct proc_map_line* out)
{
	if (line == NULL || out == NULL)
		return false;

	memset(out, 0, sizeof(*out));

	const char* p = _ScanHex(line, &out->start);
	if (p == NULL || *p != '-')
		return false;

	p = _ScanHex(p + 1, &out->end);
	if (p == NULL || *p != ' ')
		return false;

	p++;
	size_t i = 0;
	while (*p != '\0' && *p != ' ' && i < sizeof(out->perms) - 1)
		out->perms[i++] = *p++;
	out->perms[i] = '\0';
	if (*p != ' ' || i == 0)
		return false;

	unsigned long long value = 0;
	p = _ScanHex(p + 1, &value);
	out->offset = value;
	if (p == NULL || *p != ' ')
		return false;

	p = _ScanHex(p + 1, &value);
	if (p == NULL || *p != ':')
		return false;
	out->devMajor = (unsigned int)value;

	p = _ScanHex(p + 1, &value);
	if (p == NULL || *p != ' ')
		return false;
	out->devMinor = (unsigned int)value;

	p = _ScanDecimal(p + 1, &out->inode);
	if (p == NULL)
		return false;

	while (*p == ' ' || *p == '\t')
		p++;

	// The kernel appends " (deleted)" to unlinked mappings and pads the
	// smaps pathname column; take the rest of the line as-is.
	const char* end = p;
	while (*end != '\0' && *end != '\n' && *end != '\r')
		end++;

	out->path = p;
	out->pathLength = (size_t)(end - p);
	return true;
}


bool
proc_read_sysfs_uint(const char* path, unsigned long* value)
{
	if (path == NULL || value == NULL)
		return false;

	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return false;

	char buffer[64];
	ssize_t length = proc_read_all(fd, buffer, sizeof(buffer));
	close(fd);
	if (length <= 0)
		return false;

	*value = strtoul(buffer, NULL, 10);
	return true;
}


long
proc_clk_tck()
{
	static volatile long sValue = 0;

	long value = __atomic_load_n(&sValue, __ATOMIC_RELAXED);
	if (value == 0) {
		value = sysconf(_SC_CLK_TCK);
		if (value <= 0)
			value = 100;
		__atomic_store_n(&sValue, value, __ATOMIC_RELAXED);
	}
	return value;
}


long
proc_page_size()
{
	static volatile long sValue = 0;

	long value = __atomic_load_n(&sValue, __ATOMIC_RELAXED);
	if (value == 0) {
		value = sysconf(_SC_PAGESIZE);
		if (value <= 0)
			value = B_PAGE_SIZE;
		__atomic_store_n(&sValue, value, __ATOMIC_RELAXED);
	}
	return value;
}


// btime is the wall clock at boot, so a clock step moves it: refresh once
// per batch of team calls. Per thread, as a team walk drains on one thread.
static __thread uint64 sBootTime = 0;


void
proc_refresh_boot_time()
{
	int fd = open("/proc/stat", O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return;

	ProcLineReader reader;
	if (!reader.OpenFd(fd))
		return;

	const char* line;
	while ((line = reader.Next()) != NULL) {
		if (strncmp(line, "btime ", 6) == 0) {
			unsigned long long bootTime = strtoull(line + 6, NULL, 10);
			if (bootTime > 0)
				sBootTime = (uint64)bootTime * 1000000;
			break;
		}
	}
}


uint64
proc_boot_time_micros()
{
	if (sBootTime == 0)
		proc_refresh_boot_time();
	return sBootTime;
}


namespace {


pthread_mutex_t sEnumLock = PTHREAD_MUTEX_INITIALIZER;
std::map<int32, void*> sEnumStates;
int32 sEnumNext = 1;


} // namespace


int32
proc_enum_alloc(void* state)
{
	if (state == NULL)
		return 0;

	pthread_mutex_lock(&sEnumLock);

	int32 cookie = sEnumNext++;
	if (sEnumNext <= 0)
		sEnumNext = 1;
	while (sEnumStates.find(cookie) != sEnumStates.end()) {
		cookie = sEnumNext++;
		if (sEnumNext <= 0)
			sEnumNext = 1;
	}
	sEnumStates[cookie] = state;

	pthread_mutex_unlock(&sEnumLock);
	return cookie;
}


void*
proc_enum_state(int32 cookie)
{
	if (cookie <= 0)
		return NULL;

	pthread_mutex_lock(&sEnumLock);

	void* state = NULL;
	std::map<int32, void*>::iterator it = sEnumStates.find(cookie);
	if (it != sEnumStates.end())
		state = it->second;

	pthread_mutex_unlock(&sEnumLock);
	return state;
}


void*
proc_enum_release(int32 cookie)
{
	if (cookie <= 0)
		return NULL;

	pthread_mutex_lock(&sEnumLock);

	void* state = NULL;
	std::map<int32, void*>::iterator it = sEnumStates.find(cookie);
	if (it != sEnumStates.end()) {
		state = it->second;
		sEnumStates.erase(it);
	}

	pthread_mutex_unlock(&sEnumLock);
	return state;
}


// Fork child: the lock may have been held at fork, and the states are the
// parent's.
void
proc_enum_reset_after_fork()
{
	pthread_mutex_init(&sEnumLock, NULL);
	sEnumStates.clear();
	sEnumNext = 1;
}


ProcDirBatch::ProcDirBatch()
	:
	fFd(-1),
	fOffset(0),
	fUsed(0)
{
}


ProcDirBatch::~ProcDirBatch()
{
	Close();
}


bool
ProcDirBatch::Open(const char* path)
{
	Close();

	fFd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	return fFd >= 0;
}


void
ProcDirBatch::Close()
{
	if (fFd >= 0) {
		close(fFd);
		fFd = -1;
	}
	fOffset = 0;
	fUsed = 0;
}


bool
ProcDirBatch::_Refill()
{
	if (fFd < 0)
		return false;

	for (;;) {
		ssize_t count = syscall(SYS_getdents64, fFd, fBuffer,
			sizeof(fBuffer));
		if (count < 0) {
			if (errno == EINTR)
				continue;
			return false;
		}

		fOffset = 0;
		fUsed = (size_t)count;
		return fUsed > 0;
	}
}


bool
ProcDirBatch::NextPid(pid_t* pid)
{
	if (pid == NULL)
		return false;

	// linux_dirent64: d_ino(8) d_off(8) d_reclen(2) d_type(1) d_name[]
	enum { kHeader = 8 + 8 + 2 + 1 };

	for (;;) {
		if (fOffset >= fUsed && !_Refill())
			return false;

		if (fUsed - fOffset < kHeader)
			return false;

		const char* base = fBuffer + fOffset;
		uint16_t reclen;
		memcpy(&reclen, base + 16, sizeof(reclen));
		if (reclen < kHeader || fOffset + reclen > fUsed)
			return false;

		const char* name = base + kHeader;
		size_t nameLength = reclen - kHeader;
		fOffset += reclen;

		if (nameLength == 0 || name[0] < '1' || name[0] > '9')
			continue;

		pid_t value = 0;
		size_t digits = 0;
		bool valid = true;
		for (size_t i = 0; i < nameLength && name[i] != '\0'; i++) {
			if (name[i] < '0' || name[i] > '9') {
				valid = false;
				break;
			}
			digits++;
			if (digits > 10) {
				valid = false;
				break;
			}
			value = value * 10 + (name[i] - '0');
		}
		if (!valid || value <= 0)
			continue;

		*pid = value;
		return true;
	}
}


ProcLineReader::ProcLineReader()
	:
	fFd(-1),
	fOffset(0),
	fUsed(0),
	fEof(false)
{
	fLine[0] = '\0';
}


ProcLineReader::~ProcLineReader()
{
	Close();
}


bool
ProcLineReader::Open(const struct proc_dir* dir, const char* name)
{
	if (dir == NULL)
		return false;

	return OpenFd(proc_dir_open_file(dir, name));
}


bool
ProcLineReader::OpenPath(const char* path)
{
	if (path == NULL)
		return false;

	return OpenFd(open(path, O_RDONLY | O_CLOEXEC));
}


bool
ProcLineReader::OpenFd(int fd)
{
	Close();

	if (fd < 0)
		return false;

	fFd = fd;
	return true;
}


void
ProcLineReader::Close()
{
	if (fFd >= 0) {
		close(fFd);
		fFd = -1;
	}
	fOffset = 0;
	fUsed = 0;
	fEof = false;
	fLine[0] = '\0';
}


bool
ProcLineReader::_Refill()
{
	fOffset = 0;
	fUsed = 0;

	for (;;) {
		ssize_t count = read(fFd, fBuffer, sizeof(fBuffer));
		if (count < 0) {
			if (errno == EINTR)
				continue;
			fEof = true;
			return false;
		}
		fUsed = (size_t)count;
		return fUsed > 0;
	}
}


const char*
ProcLineReader::Next()
{
	if (fFd < 0)
		return NULL;

	size_t used = 0;
	for (;;) {
		if (fOffset >= fUsed) {
			if (fEof)
				return NULL;
			if (!_Refill()) {
				if (used > 0)
					break;
				return NULL;
			}
		}

		const char* chunk = fBuffer + fOffset;
		size_t available = fUsed - fOffset;
		const void* nl = memchr(chunk, '\n', available);
		size_t take = (nl != NULL)
			? (size_t)((const char*)nl - chunk) : available;

		if (used + take >= kMaxLine)
			take = kMaxLine - 1 - used;
		memcpy(fLine + used, chunk, take);
		used += take;

		if (nl != NULL) {
			fOffset += (size_t)((const char*)nl - chunk) + 1;
			break;
		}

		fOffset += available;
	}

	fLine[used] = '\0';
	return fLine;
}
