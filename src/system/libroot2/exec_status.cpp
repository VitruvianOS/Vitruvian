/*
 *  Copyright 2026, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

// Needed before any system header for pipe2() and pread().
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include "exec_status.h"

#include <elf.h>
#include <errno.h>
#include <Errors.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>


namespace BKernelPrivate {


static const char* kLoaderLibError = "error while loading shared libraries:";


/*!	Extracts the PT_INTERP path of a dynamic ELF.

	Anything that is not a little-endian ELF with an interpreter (scripts,
	static binaries, garbage) yields -1; execve() stays the authority for
	those.
*/
static int
_GetElfInterpreter(const char* path, char* buffer, size_t size)
{
	buffer[0] = '\0';

	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;

	uint8 ident[EI_NIDENT];
	ssize_t bytesRead = read(fd, ident, sizeof(ident));
	if (bytesRead != (ssize_t)sizeof(ident)
		|| ident[EI_MAG0] != ELFMAG0 || ident[EI_MAG1] != ELFMAG1
		|| ident[EI_MAG2] != ELFMAG2 || ident[EI_MAG3] != ELFMAG3
		|| ident[EI_DATA] != ELFDATA2LSB) {
		close(fd);
		return -1;
	}

	off_t phoff;
	uint16 phentsize;
	uint16 phnum;
	size_t phdrSize;
	if (ident[EI_CLASS] == ELFCLASS64) {
		Elf64_Ehdr header;
		if (pread(fd, &header, sizeof(header), 0) != (ssize_t)sizeof(header)) {
			close(fd);
			return -1;
		}
		phoff = header.e_phoff;
		phentsize = header.e_phentsize;
		phnum = header.e_phnum;
		phdrSize = sizeof(Elf64_Phdr);
	} else if (ident[EI_CLASS] == ELFCLASS32) {
		Elf32_Ehdr header;
		if (pread(fd, &header, sizeof(header), 0) != (ssize_t)sizeof(header)) {
			close(fd);
			return -1;
		}
		phoff = header.e_phoff;
		phentsize = header.e_phentsize;
		phnum = header.e_phnum;
		phdrSize = sizeof(Elf32_Phdr);
	} else {
		close(fd);
		return -1;
	}

	if (phentsize < phdrSize || phnum == 0 || phnum > 1024) {
		close(fd);
		return -1;
	}

	int result = -1;
	for (uint16 i = 0; i < phnum; i++) {
		uint32 type;
		off_t offset;
		if (ident[EI_CLASS] == ELFCLASS64) {
			Elf64_Phdr phdr;
			if (pread(fd, &phdr, sizeof(phdr),
					phoff + (off_t)i * phentsize) != (ssize_t)sizeof(phdr)) {
				break;
			}
			type = phdr.p_type;
			offset = phdr.p_offset;
		} else {
			Elf32_Phdr phdr;
			if (pread(fd, &phdr, sizeof(phdr),
					phoff + (off_t)i * phentsize) != (ssize_t)sizeof(phdr)) {
				break;
			}
			type = phdr.p_type;
			offset = phdr.p_offset;
		}

		if (type != PT_INTERP)
			continue;

		ssize_t readBytes = pread(fd, buffer, size - 1, offset);
		if (readBytes <= 0)
			break;

		buffer[readBytes] = '\0';
		// the kernel requires the string to be NUL terminated inside
		// the segment; reject anything else
		if (memchr(buffer, '\0', readBytes) != NULL && buffer[0] != '\0')
			result = 0;
		break;
	}

	close(fd);
	return result;
}


/*!	Runs the loader in list mode on \a path and looks for its verdict on
	unresolvable dependencies.
*/
status_t
CheckDynamicLoader(const char* path, char* detail, size_t detailSize)
{
	if (detail != NULL && detailSize > 0)
		detail[0] = '\0';

	// bare names would need a PATH search here; execvpe() does its own
	if (path == NULL || strchr(path, '/') == NULL)
		return B_OK;

	char interpreter[PATH_MAX];
	if (_GetElfInterpreter(path, interpreter, sizeof(interpreter)) != 0)
		return B_OK;

	int pipeFd[2];
	if (pipe2(pipeFd, O_CLOEXEC) != 0)
		return B_OK;

	// Raw clone, not fork(): the child only execs, and fork() would run the
	// atfork handlers, opening /dev/nexus for a team that never lives.
	pid_t pid = syscall(SYS_clone, SIGCHLD, 0, NULL, NULL, 0);
	if (pid < 0) {
		close(pipeFd[0]);
		close(pipeFd[1]);
		return B_OK;
	}

	if (pid == 0) {
		// async-signal-safe only: this parent may be multi-threaded
		close(pipeFd[0]);
		// the loader lists on stdout and diagnoses on stderr
		dup2(pipeFd[1], STDOUT_FILENO);
		dup2(pipeFd[1], STDERR_FILENO);
		if (pipeFd[1] != STDOUT_FILENO && pipeFd[1] != STDERR_FILENO)
			close(pipeFd[1]);
		execl(interpreter, interpreter, "--list", path, (char*)NULL);
		_exit(127);
	}

	close(pipeFd[1]);

	// the loader names the offending library first; the head is enough
	char buffer[2048];
	size_t total = 0;
	for (;;) {
		ssize_t bytesRead = read(pipeFd[0], buffer + total,
			sizeof(buffer) - 1 - total);
		if (bytesRead <= 0)
			break;
		total += bytesRead;
		if (total >= sizeof(buffer) - 1)
			break;
	}
	close(pipeFd[0]);
	buffer[total] = '\0';

	int status;
	while (waitpid(pid, &status, 0) == -1 && errno == EINTR)
		;

	const char* found = strstr(buffer, kLoaderLibError);
	if (found == NULL)
		return B_OK;

	found += strlen(kLoaderLibError);
	while (*found == ' ')
		found++;

	const char* end = strchr(found, ':');
	if (end == NULL)
		end = found + strlen(found);

	if (detail != NULL && detailSize > 0) {
		size_t length = end - found;
		if (length >= detailSize)
			length = detailSize - 1;
		memcpy(detail, found, length);
		detail[length] = '\0';
	}

	return B_MISSING_LIBRARY;
}


status_t
WaitForExecStatus(int fd, bigtime_t timeout)
{
	if (fd < 0)
		return B_BAD_VALUE;

	struct pollfd pollFd;
	pollFd.fd = fd;
	pollFd.events = POLLIN;
	pollFd.revents = 0;

	int32 status = 0;
	for (;;) {
		int pollResult = poll(&pollFd, 1, (int)(timeout / 1000));
		if (pollResult < 0) {
			if (errno == EINTR)
				continue;
			return B_ERROR;
		}
		if (pollResult == 0)
			return B_TIMED_OUT;

		ssize_t bytesRead = read(fd, &status, sizeof(status));
		if (bytesRead < 0) {
			if (errno == EINTR)
				continue;
			return B_ERROR;
		}
		if (bytesRead == 0) {
			// the write end closed on a successful exec
			return B_OK;
		}
		if (bytesRead == (ssize_t)sizeof(status))
			return status;
	}
}


void
CloseExecStatusPipe(int readFd, int writeFd)
{
	if (readFd >= 0)
		close(readFd);
	if (writeFd >= 0)
		close(writeFd);
}


}


extern "C" {


/*!	Reports the loader's verdict on a binary, naming the library when one
	cannot be resolved. Used by Tracker/Deskbar for launch failure alerts.
*/
status_t
__loader_check_binary(const char* path, char* detail, size_t detailSize)
{
	return BKernelPrivate::CheckDynamicLoader(path, detail, detailSize);
}


status_t
__wait_exec_status(int fd, bigtime_t timeout)
{
	return BKernelPrivate::WaitForExecStatus(fd, timeout);
}


}
