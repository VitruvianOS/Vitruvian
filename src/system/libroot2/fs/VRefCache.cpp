/*
 * Copyright 2025-2026 Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <VRefCache.h>

#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include <map>
#include <mutex>
#include <utility>


namespace BPrivate {


namespace {


struct Entry {
	vref_id		id;
	dev_t		dev;
	ino_t		ino;
	vref_key	anchor_key;
	uint32_t	soft_refs;
};


typedef std::pair<dev_t, ino_t> DevInoKey;

static std::mutex& Lock() {
	static std::mutex m;
	return m;
}

static std::map<vref_id, Entry*>& ById() {
	static std::map<vref_id, Entry*> m;
	return m;
}

static std::map<DevInoKey, Entry*>& ByDevIno() {
	static std::map<DevInoKey, Entry*> m;
	return m;
}


void
EraseEntryLocked(Entry* e)
{
	ById().erase(e->id);
	if (e->dev != 0 || e->ino != 0)
		ByDevIno().erase({ e->dev, e->ino });
}


}


vref_id
VRefCache::AcquireFromFd(int fd)
{
	struct stat st;
	if (fstat(fd, &st) < 0)
		return B_ERROR;

	bool hasIdentity = st.st_dev != 0 && st.st_dev != (dev_t)B_INVALID_DEV
		&& st.st_ino != 0 && st.st_ino != (ino_t)B_INVALID_INO;

	DevInoKey lookup = { st.st_dev, st.st_ino };

	if (hasIdentity) {
		std::unique_lock<std::mutex> g(Lock());
		auto it = ByDevIno().find(lookup);
		if (it != ByDevIno().end()) {
			it->second->soft_refs++;
			return it->second->id;
		}
	}

	vref_key key = 0;
	vref_id id = ::create_vref(fd, &key);
	if (id < 0)
		return id;

	std::unique_lock<std::mutex> g(Lock());
	if (hasIdentity) {
		// If someone else minted a slot, let's use their slot
		auto it = ByDevIno().find(lookup);
		if (it != ByDevIno().end()) {
			it->second->soft_refs++;
			vref_id existing = it->second->id;
			g.unlock();
			::release_vref(id, key);
			return existing;
		}
		Entry* e = new Entry{ id, st.st_dev, st.st_ino, key, 1 };
		ById()[id] = e;
		ByDevIno()[lookup] = e;
	} else {
		ById()[id] = new Entry{ id, B_INVALID_DEV, B_INVALID_INO, key, 1 };
	}
	return id;
}


status_t
VRefCache::Acquire(vref_id id)
{
	if (id < 0)
		return B_BAD_VALUE;

	std::unique_lock<std::mutex> g(Lock());
	auto it = ById().find(id);
	if (it != ById().end()) {
		it->second->soft_refs++;
		return B_OK;
	}
	g.unlock();

	vref_key key = 0;
	status_t r = ::acquire_vref(id, &key);
	if (r != B_OK)
		return r;

	g.lock();
	it = ById().find(id);
	if (it != ById().end()) {
		it->second->soft_refs++;
		g.unlock();
		::release_vref(id, key);
		return B_OK;
	}
	ById()[id] = new Entry{ id, B_INVALID_DEV, B_INVALID_INO, key, 1 };
	return B_OK;
}


status_t
VRefCache::Release(vref_id id)
{
	if (id < 0)
		return B_BAD_VALUE;

	std::unique_lock<std::mutex> g(Lock());
	auto it = ById().find(id);
	if (it == ById().end())
		return B_BAD_VALUE;
	if (--it->second->soft_refs > 0)
		return B_OK;

	Entry* dead = it->second;
	vref_key anchor = dead->anchor_key;
	EraseEntryLocked(dead);
	g.unlock();

	status_t r = ::release_vref(dead->id, anchor);
	delete dead;
	return r;
}


namespace {

status_t
AdoptOne(vref_id id, vref_key key)
{
	if (id < 0 || key == 0)
		return B_BAD_VALUE;

	std::unique_lock<std::mutex> g(Lock());
	auto it = ById().find(id);
	if (it != ById().end()) {
		it->second->soft_refs++;
		g.unlock();
		::release_vref(id, key);
		return B_OK;
	}
	ById()[id] = new Entry{ id, B_INVALID_DEV, B_INVALID_INO, key, 1 };
	return B_OK;
}

} // namespace


void
VRefCache::AdoptCaps(const port_cap_out* caps, size_t count)
{
	if (caps == NULL)
		return;
	for (size_t i = 0; i < count; i++) {
		if (caps[i].kind == B_PORT_CAP_VREF)
			AdoptOne(caps[i].vref_id_, caps[i].key);
	}
}


int
VRefCache::Open(vref_id id)
{
	if (id < 0)
		return B_BAD_VALUE;

	std::unique_lock<std::mutex> g(Lock());
	auto it = ById().find(id);
	if (it == ById().end())
		return B_NOT_ALLOWED;
	vref_key key = it->second->anchor_key;
	g.unlock();
	return ::open_vref(id, key);
}


static void
PrepareForFork()
{
	Lock().lock();
}

static void
ParentAfterFork()
{
	Lock().unlock();
}

static void
ChildAfterFork()
{
	ById().clear();
	ByDevIno().clear();
	Lock().unlock();
}

struct ForkHookInstaller {
	ForkHookInstaller() {
		pthread_atfork(&PrepareForFork, &ParentAfterFork, &ChildAfterFork);
	}
};
ForkHookInstaller sForkHookInstaller;


} // namespace BPrivate
