/*
 * BUSBRoster: udev hotplug roster for the USB kit.
 *
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Hooks run on the udev monitor thread (subsystem usb, devtype
 * usb_device); Stop() joins it, then pairs removals with the adds.
 */

#include <USBKit.h>

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <new>

#include <libudev.h>

#include <List.h>
#include <OS.h>


// Owns the udev context, the monitor and the devices whose DeviceAdded()
// returned B_OK. Kept in BUSBRoster::fLooper, as the legacy layout did
// with its looper.
class RosterMonitor {
public:
							RosterMonitor(BUSBRoster *roster);
							~RosterMonitor();

			status_t		Run();
			void			QuitAndWait();

private:
			void			_Enumerate();
			void			_HandleDevice(const char *action,
								const char *node);
			void			_AddDevice(const char *node);
			void			_RemoveDevice(const char *node);
			void			_DeliverRemovals();
			static status_t	_MonitorThreadEntry(void *data);
			void			_MonitorLoop();

			BUSBRoster *	fRoster;
			udev *			fUdev;
			udev_monitor *	fMonitor;
			thread_id		fThread;
			bool			fQuit;
			BList			fDevices;
};


RosterMonitor::RosterMonitor(BUSBRoster *roster)
	:
	fRoster(roster),
	fUdev(NULL),
	fMonitor(NULL),
	fThread(-1),
	fQuit(false)
{
	fUdev = udev_new();
	if (fUdev == NULL) {
		fprintf(stderr, "USBKit: udev unavailable\n");
		return;
	}

	fMonitor = udev_monitor_new_from_netlink(fUdev, "udev");
	if (fMonitor == NULL) {
		fprintf(stderr, "USBKit: udev monitor unavailable\n");
		return;
	}

	udev_monitor_filter_add_match_subsystem_devtype(fMonitor, "usb",
		"usb_device");
	udev_monitor_enable_receiving(fMonitor);
}


RosterMonitor::~RosterMonitor()
{
	QuitAndWait();
	_DeliverRemovals();

	if (fMonitor != NULL)
		udev_monitor_unref(fMonitor);
	if (fUdev != NULL)
		udev_unref(fUdev);
}


status_t
RosterMonitor::Run()
{
	if (fMonitor == NULL)
		return B_NO_INIT;

	fThread = spawn_thread(_MonitorThreadEntry, "usb roster",
		B_NORMAL_PRIORITY, this);
	if (fThread < 0)
		return B_ERROR;

	resume_thread(fThread);
	return B_OK;
}


void
RosterMonitor::QuitAndWait()
{
	if (fThread < 0)
		return;

	fQuit = true;
	status_t result;
	wait_for_thread(fThread, &result);
	fThread = -1;
}


status_t
RosterMonitor::_MonitorThreadEntry(void *data)
{
	((RosterMonitor *)data)->_MonitorLoop();
	return B_OK;
}


void
RosterMonitor::_MonitorLoop()
{
	// Enable receiving before the first enumeration so nothing that shows
	// up meanwhile is missed; duplicate paths are filtered on add.
	_Enumerate();

	int fd = udev_monitor_get_fd(fMonitor);
	while (!fQuit) {
		struct pollfd descriptor = { fd, POLLIN, 0 };
		int result = poll(&descriptor, 1, 250);
		if (result < 0) {
			if (errno == EINTR)
				continue;
			break;
		}

		if (result == 0 || (descriptor.revents & POLLIN) == 0)
			continue;

		udev_device *device = udev_monitor_receive_device(fMonitor);
		if (device == NULL)
			continue;

		const char *action = udev_device_get_action(device);
		const char *node = udev_device_get_devnode(device);
		if (action != NULL && node != NULL)
			_HandleDevice(action, node);

		udev_device_unref(device);
	}
}


void
RosterMonitor::_Enumerate()
{
	udev_enumerate *enumerate = udev_enumerate_new(fUdev);
	if (enumerate == NULL)
		return;

	udev_enumerate_add_match_subsystem(enumerate, "usb");
	udev_enumerate_scan_devices(enumerate);

	udev_list_entry *entry;
	udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(enumerate)) {
		const char *syspath = udev_list_entry_get_name(entry);
		udev_device *device = udev_device_new_from_syspath(fUdev, syspath);
		if (device == NULL)
			continue;

		const char *node = udev_device_get_devnode(device);
		if (node != NULL)
			_HandleDevice("add", node);

		udev_device_unref(device);
	}

	udev_enumerate_unref(enumerate);
}


void
RosterMonitor::_HandleDevice(const char *action, const char *node)
{
	if (strcmp(action, "add") == 0)
		_AddDevice(node);
	else if (strcmp(action, "remove") == 0)
		_RemoveDevice(node);
}


// The kit exposes a device's location as the tail of its devnode
// ("/001/002"), which is what the roster matches on.
static const char *
location_of(const char *node)
{
	if (strlen(node) < 12)
		return node;

	return node + 12;
}


void
RosterMonitor::_AddDevice(const char *node)
{
	const char *location = location_of(node);
	for (int32 i = 0; i < fDevices.CountItems(); i++) {
		BUSBDevice *device = (BUSBDevice *)fDevices.ItemAt(i);
		const char *known = device->Location();
		if (known != NULL && strcmp(known, location) == 0)
			return;
	}

	BUSBDevice *device = new(std::nothrow) BUSBDevice(node);
	if (device == NULL)
		return;

	if (device->InitCheck() != B_OK) {
		delete device;
		return;
	}

	if (fRoster->DeviceAdded(device) == B_OK)
		fDevices.AddItem(device);
	else
		delete device;
}


void
RosterMonitor::_RemoveDevice(const char *node)
{
	const char *location = location_of(node);
	for (int32 i = 0; i < fDevices.CountItems(); i++) {
		BUSBDevice *device = (BUSBDevice *)fDevices.ItemAt(i);
		const char *known = device->Location();
		if (known == NULL || strcmp(known, location) != 0)
			continue;

		fDevices.RemoveItem(i);
		fRoster->DeviceRemoved(device);
		delete device;
		return;
	}
}


void
RosterMonitor::_DeliverRemovals()
{
	while (fDevices.CountItems() > 0) {
		BUSBDevice *device = (BUSBDevice *)fDevices.RemoveItem(
			fDevices.CountItems() - 1);
		if (device == NULL)
			continue;

		fRoster->DeviceRemoved(device);
		delete device;
	}
}


BUSBRoster::BUSBRoster()
	:
	fLooper(NULL)
{
}


BUSBRoster::~BUSBRoster()
{
	Stop();
}


void
BUSBRoster::Start()
{
	if (fLooper != NULL)
		return;

	RosterMonitor *monitor = new(std::nothrow) RosterMonitor(this);
	if (monitor == NULL)
		return;

	if (monitor->Run() != B_OK) {
		delete monitor;
		return;
	}

	fLooper = monitor;
}


void
BUSBRoster::Stop()
{
	if (fLooper == NULL)
		return;

	RosterMonitor *monitor = (RosterMonitor *)fLooper;
	fLooper = NULL;

	// The destructor joins the thread and delivers the removals for every
	// device whose DeviceAdded() returned B_OK.
	delete monitor;
}


void
BUSBRoster::_ReservedUSBRoster1()
{
}


void
BUSBRoster::_ReservedUSBRoster2()
{
}


void
BUSBRoster::_ReservedUSBRoster3()
{
}


void
BUSBRoster::_ReservedUSBRoster4()
{
}


void
BUSBRoster::_ReservedUSBRoster5()
{
}
