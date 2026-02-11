/*
 *  Copyright 2019-2026, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include "disk_monitor.h"

#include <NodeMonitor.h>

#include <sys/poll.h>
#include <libudev.h>
#include <atomic>
#include <map>
#include <vector>
#include <mutex>
#include <thread>

#include <util/KMessage.h>

#include "Team.h"
#include "KernelDebug.h"


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


static void
send_device_notification(port_id port, uint32 token, uint32 opcode, dev_t newDevice)
{
	KMessage message;
	char buffer[128];
	const int32 kPortMessageCode = 'pjpp';
	message.SetTo(buffer, sizeof(buffer), B_NODE_MONITOR);
	message.AddInt32("opcode", opcode);
	if (opcode == B_DEVICE_MOUNTED) {
		message.AddInt32("new device", (int32)newDevice);
	} else {
		message.AddInt32("device", (int32)newDevice);
	}
	message.SetDeliveryInfo(token, port, -1, find_thread(NULL));

	write_port(port, kPortMessageCode, message.Buffer(), message.ContentSize());
}


static uint32
udev_action_to_opcode(const char* action)
{
	if (!action)
		return 0;
	if (strcmp(action, "add") == 0)
		return B_DEVICE_MOUNTED;
	if (strcmp(action, "remove") == 0)
		return B_DEVICE_UNMOUNTED;
	return 0;
}


static void
udev_thread_func()
{
	printf("udev watcher thread started\n");

	struct udev_monitor* udevMonitor = nullptr;
	{
		std::lock_guard<std::mutex> guard(gWatchLock);
		udevMonitor = init_udev();
		if (!udevMonitor) {
			printf("disk_monitor: can't get udev_monitor...exiting\n");
			gWatchThreadRunning = false;
			return; 
		}
	}

	int ufd = -1;
	{
		std::lock_guard<std::mutex> guard(gWatchLock);
		if (udevMonitor)
			ufd = udev_monitor_get_fd(udevMonitor);
	}

	while (!gWatcherExitRequested) {
		if (ufd < 0) {
			std::lock_guard<std::mutex> guard(gWatchLock);
			if (udevMonitor)
				ufd = udev_monitor_get_fd(udevMonitor);
			if (ufd < 0) {
				snooze(200);
				continue;
			}
		}

		struct pollfd pfd;
		pfd.fd = ufd;
		pfd.events = POLLIN;
		pfd.revents = 0;

		int ret = poll(&pfd, 1, 500);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			break;
		}

		if (ret == 0 || !(pfd.revents & POLLIN))
			continue;

		struct udev_device* dev;
		while ((dev = udev_monitor_receive_device(udevMonitor)) != nullptr) {
			const char* action = udev_device_get_action(dev);
			const char* devnode = udev_device_get_devnode(dev);
			(void)devnode;

			uint32 opcode = udev_action_to_opcode(action);
			if (opcode != 0) {
				dev_t devnum = udev_device_get_devnum(dev);

				std::vector<VolumeWatcherKey> mountTargets;
				std::vector<VolumeWatcher> volumeTargets;
				std::vector<std::pair<VolumeWatcherKey, uint32>> diskTargets;

				{
					std::lock_guard<std::mutex> guard(gWatchLock);
					mountTargets.reserve(gMountWatchers.size());
					for (const auto& entry : gMountWatchers) {
						mountTargets.push_back(entry.first);
					}
					if (opcode == B_DEVICE_UNMOUNTED) {
						for (const auto& vw : gVolumeWatchers) {
							if (vw.device == devnum)
								volumeTargets.push_back(vw);
						}
					}
					diskTargets.reserve(gDiskWatchers.size());
					for (const auto& entry : gDiskWatchers) {
						diskTargets.emplace_back(entry.first, entry.second);
					}
				}

				for (const auto& key : mountTargets) {
					send_device_notification(key.port, key.token, opcode, devnum);
				}

				for (const auto& vw : volumeTargets) {
					send_device_notification(vw.port, vw.token, B_DEVICE_UNMOUNTED, devnum);
				}

				for (const auto& p : diskTargets) {
					const VolumeWatcherKey& key = p.first;
					uint32 mask = p.second;
					bool send = false;
					if (opcode == B_DEVICE_MOUNTED && (mask & WATCH_DISK_ADD))
						send = true;
					else if (opcode == B_DEVICE_UNMOUNTED && (mask & WATCH_DISK_REMOVE))
						send = true;
					if (send) {
						send_device_notification(key.port, key.token, opcode, devnum);
					}
				}
			}

			udev_device_unref(dev);
		}
	}

	{
		std::lock_guard<std::mutex> guard(gWatchLock);
		udev_monitor_unref(udevMonitor);
	}
	gWatchThreadRunning = false;
}


static void
start_thread()
{
	bool start = false;
	{
		std::lock_guard<std::mutex> guard(gWatchLock);
		if (!gWatchThreadRunning &&
			(!gMountWatchers.empty() || !gVolumeWatchers.empty() || !gDiskWatchers.empty())) {
			gWatchThreadRunning = true;
			gWatcherExitRequested = false;
			start = true;
		}
	}
	if (start)
		gWatchThread = std::thread(udev_thread_func);
}

static void
stop_thread()
{
	bool stop = false;
	{
		std::lock_guard<std::mutex> guard(gWatchLock);
		if (gWatchThreadRunning &&
			gMountWatchers.empty() && gVolumeWatchers.empty() && gDiskWatchers.empty()) {
			stop = true;
		}
	}
	if (stop) {
		gWatcherExitRequested = true;
		if (gWatchThread.joinable())
			gWatchThread.join();

		gWatchThreadRunning = false;
	}
}


status_t
start_mount_watching(port_id port, uint32 token)
{
	printf("start mount_watching\n");

	std::lock_guard<std::mutex> guard(gWatchLock);

	VolumeWatcherKey key = { port, token };
	gMountWatchers[key] = 1;

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
	std::lock_guard<std::mutex> guard(gWatchLock);

	VolumeWatcher watcher = { port, token, device };
	gVolumeWatchers.push_back(watcher);

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
	std::lock_guard<std::mutex> guard(gWatchLock);
	return start_volume_watching(device, port, token);
}


status_t
_kern_stop_watching_volume(dev_t device, port_id port, uint32 token)
{
	std::lock_guard<std::mutex> guard(gWatchLock);
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

	printf("start_watching_disks: mask=0x%x, port=%d, token=%d",
		eventMask, port, token);

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
	stop_thread();
	return B_OK;
}


} // namespace BKernelPrivate
