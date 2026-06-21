/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <VRefTrack.h>

#include <atomic>
#include <cstddef>
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <Entry.h>
#include <Node.h>


namespace BPrivate {


static std::atomic<int> sState{0};
static FILE* sLog = NULL;
static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;


static void
_init_locked()
{
	const char* dir = getenv("VREF_TRACE");
	if (dir == NULL || dir[0] == '\0')
		dir = "/var/log/vref";

	mkdir(dir, 0755);

	char comm[64] = "unknown";
	int fd = open("/proc/self/comm", O_RDONLY);
	if (fd >= 0) {
		ssize_t n = read(fd, comm, sizeof(comm) - 1);
		close(fd);
		if (n > 0) {
			comm[n] = '\0';
			for (ssize_t i = 0; i < n; i++) {
				if (comm[i] == '\n' || comm[i] == '/' || comm[i] == ' ')
					comm[i] = '_';
			}
		}
	}

	char path[512];
	snprintf(path, sizeof(path), "%s/%s.%d.log", dir, comm, (int)getpid());
	sLog = fopen(path, "a");
	if (sLog == NULL) {
		sState.store(-1);
		return;
	}
	setvbuf(sLog, NULL, _IOLBF, 0);
	sState.store(1);
}


bool
vref_track_enabled()
{
	int s = sState.load(std::memory_order_acquire);
	if (s != 0)
		return s > 0;

	pthread_mutex_lock(&sLock);
	if (sState.load() == 0)
		_init_locked();
	pthread_mutex_unlock(&sLock);
	return sState.load() > 0;
}


static const char*
_op_name(vref_track_event op)
{
	switch (op) {
		case VREF_TRACK_EREF_CTOR_DEFAULT:	return "EREF.ctor()";
		case VREF_TRACK_EREF_CTOR_DEVDIR:	return "EREF.ctor(dev,dir,name)";
		case VREF_TRACK_EREF_CTOR_FD:		return "EREF.ctor(fd,name)";
		case VREF_TRACK_EREF_CTOR_NODE:		return "EREF.ctor(node,name)";
		case VREF_TRACK_EREF_CTOR_COPY:		return "EREF.ctor(copy)";
		case VREF_TRACK_EREF_DTOR:			return "EREF.dtor";
		case VREF_TRACK_EREF_ASSIGN:		return "EREF.assign";
		case VREF_TRACK_EREF_SET_NAME:		return "EREF.set_name";
		case VREF_TRACK_EREF_FLATTEN:		return "EREF.Flatten";
		case VREF_TRACK_EREF_UNFLATTEN:		return "EREF.Unflatten";

		case VREF_TRACK_NREF_CTOR_DEFAULT:	return "NREF.ctor()";
		case VREF_TRACK_NREF_CTOR_DEVNODE:	return "NREF.ctor(dev,node)";
		case VREF_TRACK_NREF_CTOR_FD:		return "NREF.ctor(fd)";
		case VREF_TRACK_NREF_CTOR_COPY:		return "NREF.ctor(copy)";
		case VREF_TRACK_NREF_DTOR:			return "NREF.dtor";
		case VREF_TRACK_NREF_ASSIGN:		return "NREF.assign";

		case VREF_TRACK_VREF_ACQUIRE:		return "VREF.acquire";
		case VREF_TRACK_VREF_RELEASE:		return "VREF.release";
		case VREF_TRACK_VREF_OPEN:			return "VREF.open";
		case VREF_TRACK_VREF_ACQUIRE_FROM_FD: return "VREF.acquire_from_fd";
		case VREF_TRACK_VREF_CREATE:		return "VREF.create";

		case VREF_TRACK_MSG_ADD_REF:		return "MSG.AddRef";
		case VREF_TRACK_MSG_FIND_REF:		return "MSG.FindRef";
		case VREF_TRACK_MSG_COLLECT_CAP:	return "MSG.CollectCap";
	}
	return "?";
}


static void
_caller_str(const void* caller, char* out, size_t outSize)
{
	if (caller == NULL) {
		snprintf(out, outSize, "?");
		return;
	}
	// Raw pointer only — dladdr is too expensive on hot paths. Resolve
	// offline with addr2line against the running binary if needed.
	snprintf(out, outSize, "%p", caller);
}


static void
_emit_header(char* buf, size_t bufSize)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	pid_t tid = (pid_t)syscall(SYS_gettid);
	snprintf(buf, bufSize, "%lld.%06lld tid=%d ",
		(long long)tv.tv_sec, (long long)tv.tv_usec, (int)tid);
}


static void
_emitf(const char* prefix, const char* fmt, ...)
{
	if (sLog == NULL)
		return;
	char hdr[64];
	_emit_header(hdr, sizeof(hdr));

	va_list ap;
	va_start(ap, fmt);
	pthread_mutex_lock(&sLock);
	fputs(hdr, sLog);
	fputs(prefix, sLog);
	fputc(' ', sLog);
	vfprintf(sLog, fmt, ap);
	fputc('\n', sLog);
	pthread_mutex_unlock(&sLock);
	va_end(ap);
}


void
vref_track_eref(vref_track_event op, const entry_ref* ref,
	const void* caller)
{
	if (!vref_track_enabled())
		return;
	char site[160];
	_caller_str(caller, site, sizeof(site));
	if (ref == NULL) {
		_emitf(_op_name(op), "this=NULL at=%s", site);
		return;
	}
	_emitf(_op_name(op),
		"this=%p dev=%d dir=%lld name=\"%s\" at=%s",
		(const void*)ref, (int)ref->device, (long long)ref->directory,
		ref->name ? ref->name : "", site);
}


void
vref_track_nref(vref_track_event op, const node_ref* ref,
	const void* caller)
{
	if (!vref_track_enabled())
		return;
	char site[160];
	_caller_str(caller, site, sizeof(site));
	if (ref == NULL) {
		_emitf(_op_name(op), "this=NULL at=%s", site);
		return;
	}
	_emitf(_op_name(op),
		"this=%p dev=%d node=%lld at=%s",
		(const void*)ref, (int)ref->device, (long long)ref->node, site);
}


void
vref_track_vref(vref_track_event op, uint32 id, const void* origin,
	const void* caller)
{
	if (!vref_track_enabled())
		return;
	char site[160];
	_caller_str(caller, site, sizeof(site));
	_emitf(_op_name(op), "id=%u origin=%p at=%s",
		(unsigned)id, origin, site);
}


static void
_hex32(const void* buffer, size_t size, char* out, size_t outSize)
{
	const unsigned char* p = (const unsigned char*)buffer;
	size_t n = size < 32 ? size : 32;
	size_t pos = 0;
	for (size_t i = 0; i < n && pos + 3 < outSize; i++)
		pos += snprintf(out + pos, outSize - pos, "%02x", p[i]);
	out[pos] = '\0';
}


void
vref_track_flatten(vref_track_event op, const entry_ref* eref,
	const node_ref* nref, const void* buffer, size_t size,
	const void* caller)
{
	if (!vref_track_enabled())
		return;
	char site[160];
	_caller_str(caller, site, sizeof(site));
	char hex[96];
	_hex32(buffer, size, hex, sizeof(hex));
	if (eref != NULL) {
		_emitf(_op_name(op),
			"this=%p dev=%d dir=%lld name=\"%s\" size=%zu hex=%s at=%s",
			(const void*)eref, (int)eref->device, (long long)eref->directory,
			eref->name ? eref->name : "",
			size, hex, site);
	} else if (nref != NULL) {
		_emitf(_op_name(op),
			"this=%p dev=%d node=%lld size=%zu hex=%s at=%s",
			(const void*)nref, (int)nref->device, (long long)nref->node,
			size, hex, site);
	} else {
		_emitf(_op_name(op), "size=%zu hex=%s at=%s", size, hex, site);
	}
}


void
vref_track_msg(vref_track_event op, uint32 what, const char* name,
	const entry_ref* eref, const void* caller)
{
	if (!vref_track_enabled())
		return;
	char site[160];
	_caller_str(caller, site, sizeof(site));
	char what4[5];
	what4[0] = (char)((what >> 24) & 0xff);
	what4[1] = (char)((what >> 16) & 0xff);
	what4[2] = (char)((what >>  8) & 0xff);
	what4[3] = (char)((what      ) & 0xff);
	what4[4] = '\0';
	for (int i = 0; i < 4; i++) {
		if (!isprint((unsigned char)what4[i]))
			what4[i] = '.';
	}
	if (eref != NULL) {
		_emitf(_op_name(op),
			"what=%s field=\"%s\" dev=%d dir=%lld name=\"%s\" at=%s",
			what4, name ? name : "",
			(int)eref->device, (long long)eref->directory,
			eref->name ? eref->name : "", site);
	} else {
		_emitf(_op_name(op), "what=%s field=\"%s\" at=%s",
			what4, name ? name : "", site);
	}
}


} // namespace BPrivate
