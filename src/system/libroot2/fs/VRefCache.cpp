/*
 * Copyright 2025-2026 Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <VRefCache.h>

#include <VRefTrack.h>

#include <pthread.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <map>
#include <mutex>
#include <unordered_set>
#include <utility>


namespace BPrivate {
namespace {


struct Entry {
	vref_id		id;
	dev_t		dev;
	ino_t		ino;
	vref_key	anchor_key;
	std::unordered_set<vref_ticket>	tickets;
};


typedef std::pair<dev_t, ino_t> DevInoKey;


static std::mutex&
Lock()
{
	static std::mutex m;
	return m;
}


static std::map<vref_id, Entry*>&
ById()
{
	static std::map<vref_id, Entry*> m;
	return m;
}


static std::map<DevInoKey, Entry*>&
ByDevIno()
{
	static std::map<DevInoKey, Entry*> m;
	return m;
}


static std::atomic<uint64_t> sNextTicket{1};


static inline vref_ticket
MintTicket()
{
	return sNextTicket.fetch_add(1, std::memory_order_relaxed);
}


static inline bool
ValidIdentity(dev_t dev, ino_t ino)
{
	return dev != 0 && dev != (dev_t)B_INVALID_DEV
		&& ino != 0 && ino != (ino_t)B_INVALID_INO;
}


static vref_ticket
AttachLocked(Entry* e)
{
	vref_ticket t = MintTicket();
	e->tickets.insert(t);
	return t;
}


static vref_ticket
InsertNewLocked(vref_id id, dev_t dev, ino_t ino, vref_key key)
{
	Entry* e = new Entry{ id, dev, ino, key, {} };
	vref_ticket t = AttachLocked(e);
	ById()[id] = e;
	if (ValidIdentity(dev, ino))
		ByDevIno()[{ dev, ino }] = e;
	return t;
}


static void
EraseEntryLocked(Entry* e)
{
	ById().erase(e->id);
	if (ValidIdentity(e->dev, e->ino))
		ByDevIno().erase({ e->dev, e->ino });
}


} // namespace


vref_handle
VRefCache::AcquireFromFd(int fd)
{
	VREF_TRACK_VREF(VREF_TRACK_VREF_ACQUIRE_FROM_FD, (uint32)fd, NULL);

	vref_handle out = { (vref_id)B_ERROR, B_INVALID_VREF_TICKET };

	struct stat st;
	if (fstat(fd, &st) < 0)
		return out;

	bool hasIdentity = ValidIdentity(st.st_dev, st.st_ino);
	DevInoKey lookup = { st.st_dev, st.st_ino };

	if (hasIdentity) {
		std::unique_lock<std::mutex> g(Lock());
		auto it = ByDevIno().find(lookup);
		if (it != ByDevIno().end()) {
			out.id = it->second->id;
			out.ticket = AttachLocked(it->second);
			return out;
		}
	}

	vref_key key = 0;
	vref_id id = ::create_vref(fd, &key);
	if (id < 0) {
		out.id = id;
		return out;
	}

	std::unique_lock<std::mutex> g(Lock());

	if (hasIdentity) {
		// Race: another thread minted a slot for the same (dev, ino)
		// while we were in create_vref. Use theirs, drop ours.
		auto it = ByDevIno().find(lookup);
		if (it != ByDevIno().end()) {
			out.id = it->second->id;
			out.ticket = AttachLocked(it->second);
			g.unlock();
			::release_vref(id, key);
			return out;
		}
		out.id = id;
		out.ticket = InsertNewLocked(id, st.st_dev, st.st_ino, key);
		return out;
	}

	out.id = id;
	out.ticket = InsertNewLocked(id, B_INVALID_DEV, B_INVALID_INO, key);
	return out;
}


vref_ticket
VRefCache::Acquire(vref_id id)
{
	VREF_TRACK_VREF(VREF_TRACK_VREF_ACQUIRE, id, NULL);

	if (id < 0)
		return B_INVALID_VREF_TICKET;

	{
		std::unique_lock<std::mutex> g(Lock());
		auto it = ById().find(id);
		if (it != ById().end())
			return AttachLocked(it->second);
	}

	vref_key key = 0;
	if (::acquire_vref(id, &key) != B_OK)
		return B_INVALID_VREF_TICKET;

	std::unique_lock<std::mutex> g(Lock());
	auto it = ById().find(id);
	if (it != ById().end()) {
		vref_ticket t = AttachLocked(it->second);
		g.unlock();
		::release_vref(id, key);
		return t;
	}
	return InsertNewLocked(id, B_INVALID_DEV, B_INVALID_INO, key);
}


status_t
VRefCache::Release(vref_id id, vref_ticket ticket)
{
	VREF_TRACK_VREF(VREF_TRACK_VREF_RELEASE, id, NULL);

	if (id < 0 || ticket == B_INVALID_VREF_TICKET)
		return B_BAD_VALUE;

	std::unique_lock<std::mutex> g(Lock());
	auto it = ById().find(id);
	if (it == ById().end())
		return B_BAD_VALUE;

	Entry* entry = it->second;
	auto ti = entry->tickets.find(ticket);
	if (ti == entry->tickets.end()) {
		// Ticket never issued for this id: reject without touching
		// other holders' slots — the point of slot+key.
		return B_BAD_VALUE;
	}
	entry->tickets.erase(ti);
	if (!entry->tickets.empty())
		return B_OK;

	vref_key anchor = entry->anchor_key;
	vref_id deadId = entry->id;
	EraseEntryLocked(entry);
	g.unlock();

	status_t r = ::release_vref(deadId, anchor);
	delete entry;
	return r;
}


namespace {


static vref_ticket
AdoptOne(vref_id id, vref_key key)
{
	if (id < 0 || key == 0)
		return B_INVALID_VREF_TICKET;

	std::unique_lock<std::mutex> g(Lock());
	auto it = ById().find(id);
	if (it != ById().end()) {
		vref_ticket t = AttachLocked(it->second);
		g.unlock();
		::release_vref(id, key);
		return t;
	}
	return InsertNewLocked(id, B_INVALID_DEV, B_INVALID_INO, key);
}


} // namespace


void
VRefCache::AdoptCaps(const port_cap_out* caps, size_t count,
	vref_ticket* tickets)
{
	if (caps == NULL)
		return;

	for (size_t i = 0; i < count; i++) {
		vref_ticket t = B_INVALID_VREF_TICKET;
		if (caps[i].kind == B_PORT_CAP_VREF)
			t = AdoptOne(caps[i].vref_id_, caps[i].key);
		if (tickets != NULL)
			tickets[i] = t;
	}
}


int
VRefCache::Open(vref_id id)
{
	VREF_TRACK_VREF(VREF_TRACK_VREF_OPEN, id, NULL);

	if (id < 0)
		return B_BAD_VALUE;

	vref_key key;
	vref_ticket guard;
	bool needIdentity;
	{
		std::unique_lock<std::mutex> g(Lock());
		auto it = ById().find(id);
		if (it == ById().end())
			return B_NOT_ALLOWED;
		key = it->second->anchor_key;
		needIdentity = !ValidIdentity(it->second->dev, it->second->ino);
		guard = AttachLocked(it->second);
	}

	int fd = ::open_vref(id, key);

	if (fd >= 0 && needIdentity) {
		struct stat st;
		if (fstat(fd, &st) == 0 && ValidIdentity(st.st_dev, st.st_ino)) {
			std::unique_lock<std::mutex> g(Lock());
			auto it = ById().find(id);
			if (it != ById().end()
				&& !ValidIdentity(it->second->dev, it->second->ino)) {
				it->second->dev = st.st_dev;
				it->second->ino = st.st_ino;
				ByDevIno()[{ st.st_dev, st.st_ino }] = it->second;
			}
		}
	}

	Release(id, guard);
	return fd;
}


namespace {


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
	// The child inherited the kernel slots; release each before
	// clearing so they don't leak for the child's lifetime.
	for (auto& pair : ById()) {
		Entry* e = pair.second;
		::release_vref(e->id, e->anchor_key);
		delete e;
	}
	ById().clear();
	ByDevIno().clear();
	Lock().unlock();
}


struct ForkHookInstaller {
	ForkHookInstaller()
	{
		pthread_atfork(&PrepareForFork, &ParentAfterFork, &ChildAfterFork);
	}
};

static ForkHookInstaller sForkHookInstaller;


} // namespace
} // namespace BPrivate
