/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "MountInfo.h"

#include <atomic>
#include <mutex>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>


namespace BPrivate {


static std::mutex				sLock;
static MountSnapshot			sSnapshot;
static std::atomic<uint64_t>	sGeneration{0};
static std::atomic<bool>		sDirty{true};
static std::atomic<bool>		sFallbackWarned{false};


// /proc/self/mountinfo octal escapes: \040 \011 \012 \134
static void
unescape_mountinfo(BString& s)
{
	const char* in = s.String();
	BString out;
	while (*in) {
		if (in[0] == '\\' && isdigit((unsigned char)in[1])
				&& isdigit((unsigned char)in[2])
				&& isdigit((unsigned char)in[3])) {
			int c = (in[1] - '0') * 64 + (in[2] - '0') * 8 + (in[3] - '0');
			out.Append((char)c, 1);
			in += 4;
		} else {
			out.Append(in, 1);
			in++;
		}
	}
	s = out;
}


static uint32_t
parse_opts_flags(const char* opts)
{
	uint32_t f = 0;
	if (opts == NULL)
		return f;
	const char* p = opts;
	while (*p) {
		const char* end = strchr(p, ',');
		size_t len = end ? (size_t)(end - p) : strlen(p);
		if (len == 2 && strncmp(p, "ro", 2) == 0)
			f |= MS_RDONLY;
		else if (len == 6 && strncmp(p, "nosuid", 6) == 0)
			f |= MS_NOSUID;
		else if (len == 5 && strncmp(p, "nodev", 5) == 0)
			f |= MS_NODEV;
		else if (len == 6 && strncmp(p, "noexec", 6) == 0)
			f |= MS_NOEXEC;
		if (!end)
			break;
		p = end + 1;
	}
	return f;
}


static bool
parse_mountinfo(MountEntryList& out)
{
	FILE* f = fopen("/proc/self/mountinfo", "r");
	if (f == NULL)
		return false;

	char* line = NULL;
	size_t cap = 0;
	ssize_t n;
	while ((n = getline(&line, &cap, f)) != -1) {
		// mountinfo fields:
		//   id parent major:minor root mount_point opts - fstype source super_opts
		int id = 0, parent = 0, major = 0, minor = 0;
		char root[4096], mnt[4096], opts[4096];
		// %ms not used to keep buffers stack-local.
		int consumed = 0;
		if (sscanf(line, "%d %d %d:%d %4095s %4095s %4095s%n",
				&id, &parent, &major, &minor, root, mnt, opts, &consumed) < 7)
			continue;

		// Skip optional fields up to '-' separator.
		char* rest = line + consumed;
		char* sep = strstr(rest, " - ");
		if (sep == NULL)
			continue;
		char fstype[256] = {0};
		char source[4096] = {0};
		char super[4096] = {0};
		if (sscanf(sep + 3, "%255s %4095s %4095s",
				fstype, source, super) < 2)
			continue;

		MountEntry e;
		e.dev = makedev(major, minor);
		e.mount_point = mnt;
		unescape_mountinfo(e.mount_point);
		e.device_path = source;
		unescape_mountinfo(e.device_path);
		e.fs_type = fstype;
		e.opts = opts;
		e.flags = parse_opts_flags(opts) | parse_opts_flags(super);
		e.mountinfo_id = id;
		e.parent_id = parent;
		e.is_bind = false;
		out.push_back(e);
	}
	free(line);
	fclose(f);

	// Mark bind clones: same dev_t already seen earlier.
	for (size_t i = 0; i < out.size(); i++) {
		for (size_t j = 0; j < i; j++) {
			if (out[i].dev == out[j].dev) {
				out[i].is_bind = true;
				break;
			}
		}
	}
	return true;
}


// Fallback when /proc/self/mountinfo is unavailable (some containers).
// /proc/mounts lacks major:minor — we stat() each mount point, which
// inherits the bind-mount disambiguation weakness the new code is
// supposed to fix. One-time syslog warning.
static bool
parse_mounts_fallback(MountEntryList& out)
{
	FILE* f = setmntent("/proc/mounts", "r");
	if (f == NULL)
		return false;

	if (!sFallbackWarned.exchange(true)) {
		syslog(LOG_WARNING,
			"MountInfo: /proc/self/mountinfo unavailable, "
			"falling back to /proc/mounts (degraded: bind-mount "
			"disambiguation disabled)");
	}

	struct mntent buf;
	char strbuf[4096];
	struct mntent* m;
	int fakeId = 1;
	while ((m = getmntent_r(f, &buf, strbuf, sizeof(strbuf))) != NULL) {
		struct stat st;
		if (stat(m->mnt_dir, &st) != 0)
			continue;

		MountEntry e;
		e.dev = st.st_dev;
		e.mount_point = m->mnt_dir;
		e.device_path = m->mnt_fsname;
		e.fs_type = m->mnt_type;
		e.opts = m->mnt_opts;
		e.flags = parse_opts_flags(m->mnt_opts);
		e.mountinfo_id = fakeId++;
		e.parent_id = 0;
		e.is_bind = false;
		out.push_back(e);
	}
	endmntent(f);
	return !out.empty();
}


static MountSnapshot
load_snapshot_locked()
{
	auto list = std::make_shared<MountEntryList>();
	if (!parse_mountinfo(*list))
		parse_mounts_fallback(*list);
	return std::static_pointer_cast<const MountEntryList>(list);
}


MountSnapshot
MountInfo::Snapshot()
{
	if (sDirty.load(std::memory_order_acquire)) {
		std::lock_guard<std::mutex> g(sLock);
		if (sDirty.load(std::memory_order_relaxed)) {
			sSnapshot = load_snapshot_locked();
			sDirty.store(false, std::memory_order_release);
		}
		return sSnapshot;
	}
	std::lock_guard<std::mutex> g(sLock);
	if (!sSnapshot)
		sSnapshot = load_snapshot_locked();
	return sSnapshot;
}


uint64_t
MountInfo::Generation()
{
	return sGeneration.load(std::memory_order_acquire);
}


void
MountInfo::Invalidate()
{
	sGeneration.fetch_add(1, std::memory_order_acq_rel);
	sDirty.store(true, std::memory_order_release);
}


bool
MountInfo::FindByDev(dev_t dev, MountEntry* out)
{
	auto snap = Snapshot();
	const MountEntry* fallback = NULL;
	for (const auto& e : *snap) {
		if (e.dev != dev)
			continue;
		if (!e.is_bind) {
			if (out) *out = e;
			return true;
		}
		if (fallback == NULL)
			fallback = &e;
	}
	if (fallback) {
		if (out) *out = *fallback;
		return true;
	}
	return false;
}


bool
MountInfo::FindByDevicePath(const char* devPath, MountEntry* out)
{
	if (devPath == NULL)
		return false;
	auto snap = Snapshot();
	for (const auto& e : *snap) {
		if (e.device_path == devPath) {
			if (out) *out = e;
			return true;
		}
	}
	return false;
}


bool
MountInfo::FindByMountPoint(const char* path, MountEntry* out)
{
	if (path == NULL)
		return false;
	auto snap = Snapshot();
	for (const auto& e : *snap) {
		if (e.mount_point == path) {
			if (out) *out = e;
			return true;
		}
	}
	return false;
}


bool
MountInfo::LongestPrefix(const char* path, MountEntry* out)
{
	if (path == NULL)
		return false;
	auto snap = Snapshot();
	const MountEntry* best = NULL;
	size_t bestLen = 0;
	size_t pathLen = strlen(path);
	for (const auto& e : *snap) {
		size_t mlen = (size_t)e.mount_point.Length();
		if (mlen == 0 || mlen > pathLen)
			continue;
		if (strncmp(e.mount_point.String(), path, mlen) != 0)
			continue;
		// Mount point ends at "/" or full match.
		if (mlen == pathLen || path[mlen] == '/' || e.mount_point == "/") {
			if (mlen > bestLen) {
				best = &e;
				bestLen = mlen;
			}
		}
	}
	if (best == NULL)
		return false;
	if (out) *out = *best;
	return true;
}


} // namespace BPrivate
