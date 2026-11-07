/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "MachineInfoReader.h"

#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>

#include <fstream>
#include <sstream>

#include "SysfsRecord.h"


namespace Hardware {


static std::string
format_kib(uint64_t kib)
{
	char text[64];
	if (kib >= 1024 * 1024)
		snprintf(text, sizeof(text), "%.1f GiB",
			(double)kib / (1024.0 * 1024.0));
	else
		snprintf(text, sizeof(text), "%.0f MiB", (double)kib / 1024.0);

	return text;
}


static std::string
format_khz(uint64_t khz)
{
	char text[64];
	if (khz >= 1000 * 1000)
		snprintf(text, sizeof(text), "%.2f GHz",
			(double)khz / (1000.0 * 1000.0));
	else
		snprintf(text, sizeof(text), "%.0f MHz", (double)khz / 1000.0);

	return text;
}


static std::string
read_cpuinfo_field(const std::string& procRoot, const char* field)
{
	std::ifstream file((procRoot + "/cpuinfo").c_str());
	if (!file.is_open())
		return std::string();

	size_t fieldLength = strlen(field);
	std::string line;
	while (std::getline(file, line)) {
		if (line.compare(0, fieldLength, field) != 0)
			continue;

		size_t colon = line.find(':');
		if (colon == std::string::npos)
			continue;

		std::string value = line.substr(colon + 1);
		size_t start = value.find_first_not_of(" \t");
		if (start == std::string::npos)
			continue;

		return value.substr(start);
	}

	return std::string();
}


static int
count_cpuinfo_processors(const std::string& procRoot)
{
	std::ifstream file((procRoot + "/cpuinfo").c_str());
	if (!file.is_open())
		return 0;

	int count = 0;
	std::string line;
	while (std::getline(file, line)) {
		if (line.compare(0, 9, "processor") == 0)
			count++;
	}

	return count;
}


MachineInfo
ReadMachineInfo(const std::string& sysRoot, const std::string& procRoot)
{
	MachineInfo info;

	std::string dmiRoot = sysRoot + "/class/dmi/id";
	info.model = ReadTrimmedFile(dmiRoot + "/product_name");
	bool isDeviceTree = false;
	if (info.model.empty()) {
		info.model = ReadTrimmedFile(sysRoot
			+ "/firmware/devicetree/base/model");
		isDeviceTree = !info.model.empty();
	}

	info.vendor = ReadTrimmedFile(dmiRoot + "/sys_vendor");

	info.cpuModel = read_cpuinfo_field(procRoot, "model name");
	if (info.cpuModel.empty())
		info.cpuModel = read_cpuinfo_field(procRoot, "Model");
	if (info.cpuModel.empty()) {
		std::string hardware = read_cpuinfo_field(procRoot, "Hardware");
		if (!hardware.empty())
			info.cpuModel = hardware;
	}

	int processors = count_cpuinfo_processors(procRoot);
	if (processors > 0) {
		char text[32];
		snprintf(text, sizeof(text), "%d", processors);
		info.cpuCores = text;
	}

	std::string maxFreq = ReadTrimmedFile(sysRoot
		+ "/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
	if (!maxFreq.empty())
		info.cpuFrequency = format_khz(strtoull(maxFreq.c_str(), NULL, 10));
	else {
		std::string mhz = read_cpuinfo_field(procRoot, "cpu MHz");
		if (!mhz.empty())
			info.cpuFrequency = mhz + " MHz";
	}

	std::string memTotal = ReadTrimmedFile(procRoot + "/meminfo");
	size_t memKey = memTotal.find("MemTotal:");
	if (memKey != std::string::npos) {
		uint64_t kib = strtoull(memTotal.c_str() + memKey + 9, NULL, 10);
		if (kib > 0)
			info.memory = format_kib(kib);
	}

	std::string biosVersion = ReadTrimmedFile(dmiRoot + "/bios_version");
	std::string biosDate = ReadTrimmedFile(dmiRoot + "/bios_date");
	bool isEfi = false;
	{
		std::ifstream efi((sysRoot + "/firmware/efi").c_str());
		isEfi = efi.is_open();
	}

	if (isEfi) {
		info.firmware = "UEFI";
		if (!biosVersion.empty())
			info.firmware += " " + biosVersion;
		if (!biosDate.empty())
			info.firmware += " (" + biosDate + ")";
	} else if (!biosVersion.empty()) {
		info.firmware = "BIOS " + biosVersion;
		if (!biosDate.empty())
			info.firmware += " (" + biosDate + ")";
	} else if (isDeviceTree) {
		info.firmware = "Device tree";
	}

	struct utsname names;
	if (uname(&names) == 0) {
		info.kernel = names.release;
		info.architecture = names.machine;
	}

	return info;
}


}	// namespace Hardware
