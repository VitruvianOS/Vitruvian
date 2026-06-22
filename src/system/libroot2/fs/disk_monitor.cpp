/*
 *  Copyright 2026, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include "disk_monitor.h"

#include <DiskDeviceRoster.h>
#include <NodeMonitor.h>

#include <mntent.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <libudev.h>
#include <atomic>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <mutex>
#include <thread>

#include <util/KMessage.h>

#include "disk_device.h"
#include "fs_type_filter.h"
#include "Team.h"
#include "KernelDebug.h"

#include <MountInfo.h>


namespace BKernelPrivate {


struct VolumeWatcherKey {
	port_id port;
	uint32 token;

	bool operator<(const VolumeWatcherKey& o) const {
		if (port != o.port)
			return port < o.port;

		return token < o.token;
	}
};

struct VolumeWatcher {
	port_id port;
	uint32 token;
	dev_t device;
};

static std::map<VolumeWatcherKey, int> gMountWatchers;
static std::vector<VolumeWatcher> gVolumeWatchers;
static std::map<VolumeWatcherKey, uint32> gDiskWatchers;

static constexpr uint32 WATCH_DISK_ADD = 0x1;
static constexpr uint32 WATCH_DISK_REMOVE = 0x2;

static std::mutex gWatchLock;
static std::atomic<bool> gWatchThreadRunning{false};
static std::atomic<bool> gWatcherExitRequested{false};
static std::thread gWatchThread;


static struct udev_monitor*
init_udev()
{
	struct udev* gUdev = Team::GetUDev();
	if (!gUdev)
		return nullptr;

	struct udev_monitor* udevMonitor = udev_monitor_new_from_netlink(gUdev, "udev");
	if (!udevMonitor)
		return nullptr;

	udev_monitor_filter_add_match_subsystem_devtype(udevMonitor, "block", nullptr);

	if (udev_monitor_enable_receiving(udevMonitor) < 0) {
		udev_monitor_unref(udevMonitor);
		return nullptr;
	}

	int ufd = udev_monitor_get_fd(udevMonitor);
	int flags = fcntl(ufd, F_GETFL, 0);
	fcntl(ufd, F_SETFL, flags | O_NONBLOCK);

	return udevMonitor;
}


// Snapshot of currently mounted dev_t's (pseudo-fs excluded). Reads via
// BPrivate::MountInfo so the cache and the watcher share one source.
static std::set<dev_t>
snapshot_mounts()
{
	std::set<dev_t> result;
	auto snap = BPrivate::MountInfo::Snapshot();
	for (const auto& e : *snap) {
		if (fs_mnttype_is_pseudo(e.fs_type.String()))
			continue;
		result.insert(e.dev);
	}
	return result;
}


static void
send_device_notification(port_id port, uint32 token, uint32 opcode, dev_t device,
	dev_t parentDevice = -1, ino_t parentDirectory = -1)
{
	KMessage message;
	char buffer[256];
	const int32 kPortMessageCode = 'pjpp';
	message.SetTo(buffer, sizeof(buffer), B_NODE_MONITOR);
	message.AddInt32("opcode", opcode);
	if (opcode == B_DEVICE_MOUNTED) {
		message.AddUInt64("new device", (uint64)device);
		if (parentDevice != (dev_t)-1)
			message.AddUInt64("device", (uint64)parentDevice);
		if (parentDirectory != (ino_t)-1)
			message.AddUInt64("directory", (uint64)parentDirectory);
	} else {
		message.AddUInt64("device", (uint64)device);
	}
	message.SetDeliveryInfo(token, port, -1, find_thread(NULL));

	write_port(port, kPortMessageCode, message.Buffer(), message.ContentSize());
}


static void
send_disk_device_notification(port_id port, uint32 token, uint32 event,
	const char* devNode)
{
	partition_id deviceId = B_INVALID_DEV;
	if (devNode && devNode[0])
		deviceId = make_partition_id(devNode);

	if (deviceId == B_INVALID_DEV)
		return;

	KMessage message;
	char buffer[256];
	const int32 kPortMessageCode = 'pjpp';

	message.SetTo(buffer, sizeof(buffer), B_DEVICE_UPDATE);
	message.AddInt32("event", event);
	message.AddUInt64("id", deviceId);
	message.AddUInt64("partition_id", deviceId);
	message.SetDeliveryInfo(token, port, -1, find_thread(NULL));

	write_port(port, kPortMessageCode, message.Buffer(), message.ContentSize());
}


static uint32
udev_action_to_disk_event(const char* action)
{
	if (!action)
		return 0;
	if (strcmp(action, "add") == 0)
		return B_DEVICE_ADDED;
	if (strcmp(action, "remove") == 0)
		return B_DEVICE_REMOVED;
	if (strcmp(action, "change") == 0)
		return B_DEVICE_MEDIA_CHANGED;
	return 0;
}


// Fires mount/unmount notifications by diffing mount snapshots.
static void
dispatch_mount_changes(const std::set<dev_t>& prev, const std::set<dev_t>& curr)
{
	std::vector<VolumeWatcherKey> mountTargets;
	std::vector<VolumeWatcher> volumeWatchers;

	{
		std::lock_guard<std::mutex> guard(gWatchLock);
		for (const auto& entry : gMountWatchers)
			mountTargets.push_back(entry.first);
		volumeWatchers = gVolumeWatchers;
	}

	// Newly mounted filesystems
	for (dev_t d : curr) {
		if (prev.count(d) == 0) {
			for (const auto& key : mountTargets)
				send_device_notification(key.port, key.token, B_DEVICE_MOUNTED, d);
		}
	}

	// Unmounted filesystems
	for (dev_t d : prev) {
		if (curr.count(d) == 0) {
			for (const auto& key : mountTargets)
				send_device_notification(key.port, key.token, B_DEVICE_UNMOUNTED, d);
			for (const auto& vw : volumeWatchers) {
				if (vw.device == d)
					send_device_notification(vw.port, vw.token, B_DEVICE_UNMOUNTED, d);
			}
		}
	}
}


static void
udev_thread_func()
{
	struct udev_monitor* udevMonitor = nullptr;
	{
		std::lock_guard<std::mutex> guard(gWatchLock);
		udevMonitor = init_udev();
	}

	int ufd = udevMonitor != nullptr ? udev_monitor_get_fd(udevMonitor) : -1;

	int mountsFd = open("/proc/mounts", O_RDONLY | O_CLOEXEC);

	std::set<dev_t> mountSnapshot = snapshot_mounts();

	while (!gWatcherExitRequested) {
		struct pollfd pfds[2];
		int nfds = 0;

		if (ufd >= 0) {
			pfds[nfds].fd = ufd;
			pfds[nfds].events = POLLIN;
			pfds[nfds].revents = 0;
			nfds++;
		}

		if (mountsFd >= 0) {
			pfds[nfds].fd = mountsFd;
			pfds[nfds].events = POLLERR | POLLPRI;
			pfds[nfds].revents = 0;
			nfds++;
		}

		if (nfds == 0) {
			snooze(200);
			continue;
		}

		int ret = poll(pfds, nfds, 500);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			break;
		}

		if (ret == 0)
			continue;

		// Check /proc/mounts for actual mount/unmount events
		for (int i = 0; i < nfds; i++) {
			if (pfds[i].fd == mountsFd
					&& (pfds[i].revents & (POLLERR | POLLPRI))) {
				// Invalidate first so any listener that calls fs_stat_dev
				// in response to the notification sees the post-event view.
				BPrivate::MountInfo::Invalidate();
				std::set<dev_t> newSnapshot = snapshot_mounts();
				dispatch_mount_changes(mountSnapshot, newSnapshot);
				mountSnapshot = std::move(newSnapshot);
				break;
			}
		}

		// Check udev for raw disk device events (add/remove/change)
		if (ufd >= 0 && (pfds[0].revents & POLLIN)) {
			struct udev_device* dev;
			while ((dev = udev_monitor_receive_device(udevMonitor)) != nullptr) {
				const char* action = udev_device_get_action(dev);
				const char* devNode = udev_device_get_devnode(dev);
				uint32 diskEvent = udev_action_to_disk_event(action);

				if (diskEvent != 0) {
					// udev raw events may precede a mount/unmount; invalidate
					// the snapshot so the next Snapshot() reparses.
					BPrivate::MountInfo::Invalidate();
					std::vector<std::pair<VolumeWatcherKey, uint32>> diskTargets;
					{
						std::lock_guard<std::mutex> guard(gWatchLock);
						diskTargets.reserve(gDiskWatchers.size());
						for (const auto& entry : gDiskWatchers)
							diskTargets.emplace_back(entry.first, entry.second);
					}

					for (const auto& p : diskTargets) {
						const VolumeWatcherKey& key = p.first;
						uint32 mask = p.second;
						uint32 eventToSend = 0;

						if (diskEvent == B_DEVICE_ADDED && (mask & WATCH_DISK_ADD))
							eventToSend = B_DEVICE_ADDED;
						else if (diskEvent == B_DEVICE_REMOVED && (mask & WATCH_DISK_REMOVE))
							eventToSend = B_DEVICE_REMOVED;
						else if (diskEvent == B_DEVICE_MEDIA_CHANGED && (mask & WATCH_DISK_ADD))
							eventToSend = B_DEVICE_MEDIA_CHANGED;

						if (eventToSend != 0)
							send_disk_device_notification(key.port, key.token, eventToSend, devNode);
					}
				}

				udev_device_unref(dev);
			}
		}
	}

	if (mountsFd >= 0)
		close(mountsFd);

	if (udevMonitor != nullptr) {
		std::lock_guard<std::mutex> guard(gWatchLock);
		udev_monitor_unref(udevMonitor);
	}
	gWatchThreadRunning = false;
}


static void
start_thread()
{
	{
		std::lock_guard<std::mutex> guard(gWatchLock);
		if (gWatchThreadRunning)
			return;
		if (!gMountWatchers.empty() || !gVolumeWatchers.empty() || !gDiskWatchers.empty()) {
			gWatcherExitRequested = false;
			gWatchThread = std::thread(udev_thread_func);
			gWatchThreadRunning = true;
		}
	}
}


static void
stop_thread()
{
	std::thread threadToJoin;
	{
		std::lock_guard<std::mutex> guard(gWatchLock);
		if (!gWatchThreadRunning)
			return;

		if (gMountWatchers.empty() && gVolumeWatchers.empty() && gDiskWatchers.empty()) {
			gWatcherExitRequested = true;
			if (gWatchThread.joinable())
				threadToJoin = std::move(gWatchThread);

			gWatchThreadRunning = false;
		}
	}

	if (threadToJoin.joinable())
		threadToJoin.join();
}


status_t
start_mount_watching(port_id port, uint32 token)
{
	{
		std::lock_guard<std::mutex> guard(gWatchLock);
		VolumeWatcherKey key = { port, token };
		gMountWatchers[key] = 1;
	}

	start_thread();
	return B_OK;
}


status_t
stop_mount_watching(port_id port, uint32 token)
{
	{
		std::lock_guard<std::mutex> guard(gWatchLock);
		VolumeWatcherKey key = { port, token };
		auto it = gMountWatchers.find(key);
		if (it != gMountWatchers.end()) {
			gMountWatchers.erase(it);
		}
	}
	stop_thread();
	return B_OK;
}


status_t
start_volume_watching(dev_t device, port_id port, uint32 token)
{
	{
		std::lock_guard<std::mutex> guard(gWatchLock);
		VolumeWatcher watcher = { port, token, device };
		gVolumeWatchers.push_back(watcher);
	}

	start_thread();
	return B_OK;
}


status_t
stop_volume_watching(dev_t device, port_id port, uint32 token)
{
	{
		std::lock_guard<std::mutex> guard(gWatchLock);
		for (auto it = gVolumeWatchers.begin(); it != gVolumeWatchers.end(); ++it) {
			if (it->device == device && it->port == port && it->token == token) {
				gVolumeWatchers.erase(it);
				break;
			}
		}
	}
	stop_thread();
	return B_OK;
}


void
stop_all_watching_for_target(port_id port, uint32 token)
{
	{
		std::lock_guard<std::mutex> guard(gWatchLock);
		VolumeWatcherKey key = { port, token };
		gMountWatchers.erase(key);

		for (auto it = gVolumeWatchers.begin(); it != gVolumeWatchers.end(); ) {
			if (it->port == port && it->token == token) {
				it = gVolumeWatchers.erase(it);
			} else {
				++it;
			}
		}
		gDiskWatchers.erase(key);
	}
	stop_thread();
}


__attribute__((destructor))
static void
disk_monitor_cleanup()
{
	gWatcherExitRequested = true;
	if (gWatchThread.joinable())
		gWatchThread.join();
}


} // namespace BKernelPrivate

using namespace BKernelPrivate;


extern "C" {


status_t
_kern_start_watching_volume(dev_t device, uint32 flags, port_id port, uint32 token)
{
	return start_volume_watching(device, port, token);
}


status_t
_kern_stop_watching_volume(dev_t device, port_id port, uint32 token)
{
	return stop_volume_watching(device, port, token);
}


status_t
_kern_start_watching_disks(uint32 eventMask, port_id port, int32 token)
{
	CALLED();

	{
		std::lock_guard<std::mutex> guard(gWatchLock);
		VolumeWatcherKey key = { port, (uint32)token };
		gDiskWatchers[key] = eventMask;
	}

	// TODO: we shouldn't start thread for every call
	start_thread();
	return B_OK;
}


status_t
_kern_stop_watching_disks(port_id port, int32 token)
{
	CALLED();

	{
		std::lock_guard<std::mutex> guard(gWatchLock);
		VolumeWatcherKey key = { port, (uint32)token };
		auto it = gDiskWatchers.find(key);
		if (it != gDiskWatchers.end())
			gDiskWatchers.erase(it);
	}
	// TODO: we shouldn't stop thread until there are listeners
	stop_thread();
	return B_OK;
}


} // namespace BKernelPrivate
