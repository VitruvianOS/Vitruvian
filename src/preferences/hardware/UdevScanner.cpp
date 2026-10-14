/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "UdevScanner.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <libudev.h>

#include "DeviceClassifier.h"
#include "HardwareMessages.h"
#include "SysfsRecord.h"


// Subsystems the preflet cares about; the classifier decides which of
// the enumerated devices are shown and on which tab.
static const char* kSubsystems[] = {
	"pci", "usb", "thunderbolt", "usb4", "nvme", "block", "scsi",
	"input", "hid", "serio", "sound", "drm", "net", "bluetooth",
	"video4linux", "power_supply", "acpi", "platform", "i2c", "spi",
	"gpio", "pwm", "regulator", "clk", "thermal", "hwmon", "iio",
	"mmc", "backlight", "touchscreen", "wwan", "remoteproc", "rpmsg",
	NULL
};


UdevScanner::UdevScanner()
	:
	fThread(-1),
	fQuitRequested(false),
	fRescanRequested(false)
{
	fWakePipe[0] = -1;
	fWakePipe[1] = -1;
}


UdevScanner::~UdevScanner()
{
	Stop();
}


status_t
UdevScanner::Start(const BMessenger& listener)
{
	if (fThread >= 0)
		return B_ALREADY_RUNNING;

	fListener = listener;

	if (pipe(fWakePipe) < 0)
		return B_ERROR;

	fThread = spawn_thread(_ThreadEntry, "hardware:scanner",
		B_NORMAL_PRIORITY, this);
	if (fThread < 0) {
		close(fWakePipe[0]);
		close(fWakePipe[1]);
		fWakePipe[0] = fWakePipe[1] = -1;
		return fThread;
	}

	resume_thread(fThread);
	return B_OK;
}


void
UdevScanner::Stop()
{
	if (fThread < 0)
		return;

	fQuitRequested = true;
	char byte = 'q';
	write(fWakePipe[1], &byte, 1);

	int32 status = 0;
	wait_for_thread(fThread, &status);
	fThread = -1;

	close(fWakePipe[0]);
	close(fWakePipe[1]);
	fWakePipe[0] = fWakePipe[1] = -1;
}


void
UdevScanner::RequestRescan()
{
	if (fThread < 0)
		return;

	fRescanRequested = true;
	char byte = 'r';
	write(fWakePipe[1], &byte, 1);
}


std::vector<Hardware::DeviceRecord>
UdevScanner::Records()
{
	fLock.Lock();
	std::vector<Hardware::DeviceRecord> copy = fRecords;
	fLock.Unlock();
	return copy;
}


int32
UdevScanner::_ThreadEntry(void* data)
{
	static_cast<UdevScanner*>(data)->_Run();
	return 0;
}


void
UdevScanner::_Run()
{
	struct udev* udev = udev_new();
	struct udev_monitor* monitor = NULL;
	int monitorFd = -1;

	if (udev != NULL) {
		monitor = udev_monitor_new_from_netlink(udev, "udev");
		if (monitor != NULL
			&& udev_monitor_enable_receiving(monitor) == 0)
			monitorFd = udev_monitor_get_fd(monitor);
	}

	_Scan();
	_Notify();

	while (!fQuitRequested) {
		struct pollfd fds[2];
		fds[0].fd = fWakePipe[0];
		fds[0].events = POLLIN;
		fds[1].fd = monitorFd;
		fds[1].events = POLLIN;

		int count = monitorFd >= 0 ? 2 : 1;
		if (poll(fds, count, -1) < 0) {
			if (errno == EINTR)
				continue;
			break;
		}

		bool scan = false;

		if (fds[0].revents & POLLIN) {
			char buffer[16];
			while (read(fWakePipe[0], buffer, sizeof(buffer)) > 0)
				;
			if (fQuitRequested)
				break;
			if (fRescanRequested) {
				fRescanRequested = false;
				scan = true;
			}
		}

		if (monitorFd >= 0 && (fds[1].revents & POLLIN)) {
			// Drain the queue first so one rescan covers a burst.
			struct udev_device* device;
			while ((device = udev_monitor_receive_device(monitor))
				!= NULL)
				udev_device_unref(device);
			scan = true;
		}

		if (scan) {
			_Scan();
			_Notify();
		}
	}

	if (monitor != NULL)
		udev_monitor_unref(monitor);
	if (udev != NULL)
		udev_unref(udev);
}


void
UdevScanner::_Scan()
{
	std::vector<Hardware::DeviceRecord> records;

	struct udev* udev = udev_new();
	if (udev != NULL) {
		for (int i = 0; kSubsystems[i] != NULL; i++) {
			struct udev_enumerate* enumerate
				= udev_enumerate_new(udev);
			if (enumerate == NULL)
				continue;

			udev_enumerate_add_match_subsystem(enumerate,
				kSubsystems[i]);
			udev_enumerate_scan_devices(enumerate);

			struct udev_list_entry* entry;
			udev_list_entry_foreach(entry,
				udev_enumerate_get_list_entry(enumerate)) {
				const char* syspath
					= udev_list_entry_get_name(entry);

				bool seen = false;
				for (size_t j = 0; j < records.size(); j++) {
					if (records[j].syspath == syspath) {
						seen = true;
						break;
					}
				}
				if (seen)
					continue;

				struct udev_device* device
					= udev_device_new_from_syspath(udev,
						syspath);
				if (device == NULL)
					continue;

				Hardware::DeviceRecord record;
				if (Hardware::ReadDeviceDir(syspath, record)) {
					// udev carries properties sysfs does
					// not spell out; keep them for the
					// Advanced pane.
					struct udev_list_entry* property;
					udev_list_entry_foreach(property,
						udev_device_get_properties_list_entry(
							device)) {
						const char* key
							= udev_list_entry_get_name(
								property);
						const char* value
							= udev_list_entry_get_value(
								property);
						if (key == NULL || value == NULL)
							continue;

						bool have = false;
						for (size_t j = 0;
							j < record.properties.size();
							j++) {
							if (record.properties[j].key
								== key) {
								have = true;
								break;
							}
						}
						if (!have) {
							Hardware::DeviceProperty
								property2;
							property2.key = key;
							property2.value = value;
							record.properties.push_back(
								property2);
						}
					}

					record.parentChain
						= Hardware::ParentChain(syspath);

					Hardware::Classification classification
						= Hardware::ClassifyRecord(record);
					if (classification.wanted)
						records.push_back(record);
				}

				udev_device_unref(device);
			}

			udev_enumerate_unref(enumerate);
		}

		udev_unref(udev);
	}

	fLock.Lock();
	fRecords = records;
	fLock.Unlock();
}


void
UdevScanner::_Notify()
{
	if (fListener.IsValid()) {
		BMessage message(kMsgDevicesUpdated);
		fListener.SendMessage(&message);
	}
}
