/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SYSFS_RECORD_H
#define _SYSFS_RECORD_H


#include <string>
#include <vector>

#include "HardwareModel.h"


// Fills a DeviceRecord from one sysfs device directory. Works on the
// real /sys and on fixture trees alike: the live udev scanner calls
// this for the data udev does not carry (resources, DT nodes, EDID),
// and the host tests call it directly against captured trees.


namespace Hardware {


std::string
ReadTrimmedFile(const std::string& path);


bool
ReadDeviceDir(const std::string& dir, DeviceRecord& record);


// Walks up from dir to the /sys/devices root, collecting each
// directory name; nearest parent first.
std::vector<std::string>
ParentChain(const std::string& dir);


// Parses the sysfs "resource" file into human-readable lines (IRQ,
// memory and I/O ranges) and appends them to record.resources.
void
ParsePciResourceFile(const std::string& path,
	std::vector<std::string>& resources);


// Reads the uevent file into record.properties, applying DRIVER=,
// MODALIAS=, DEVNAME=, ACPI_* and OF_* to the dedicated fields.
void
ApplyUeventFile(const std::string& path, DeviceRecord& record);


}	// namespace Hardware


#endif	// _SYSFS_RECORD_H
