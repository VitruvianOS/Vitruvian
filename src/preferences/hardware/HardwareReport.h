/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _HARDWARE_REPORT_H
#define _HARDWARE_REPORT_H


#include <string>
#include <vector>

#include "HardwareModel.h"
#include "HwdataDatabase.h"


// Plain-text full report for bug reports: machine summary, then every
// device grouped by category, roughly in the style of lspci -vv plus
// lsusb -v plus a device tree summary. Pure string building so the
// host tests can pin the output.


namespace Hardware {


std::string
BuildDeviceDetail(const DeviceRecord& record, const HwdataDatabase& names,
	bool includeProperties);


std::string
BuildReport(const MachineInfo& machine,
	const std::vector<DeviceRecord>& records,
	const HwdataDatabase& names, const std::string& generatedAt);


// Single-line display name: hwdata vendor/product when known, else the
// kernel-provided strings, else the kernel device name.
std::string
DeviceDisplayName(const DeviceRecord& record, const HwdataDatabase& names);


}	// namespace Hardware


#endif	// _HARDWARE_REPORT_H
