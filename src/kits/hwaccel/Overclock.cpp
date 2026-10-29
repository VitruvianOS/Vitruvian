/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "Overclock.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>


static const char* kDefaultSysfs = "/sys";
static const char* kDefaultBoot = "/boot";

static const char* kRpiBlockBegin = "# BEGIN VOS OVERCLOCK";
static const char* kRpiBlockEnd = "# END VOS OVERCLOCK";


static bool
_ReadFileString(const std::string& path, std::string& value)
{
	int fd = open(path.c_str(), O_RDONLY);
	if (fd < 0)
		return false;

	char buffer[512];
	ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);
	close(fd);
	if (bytes <= 0)
		return false;

	buffer[bytes] = '\0';
	value = buffer;
	while (!value.empty()
		&& (value[value.size() - 1] == '\n'
			|| value[value.size() - 1] == ' '
			|| value[value.size() - 1] == '\t')) {
		value.erase(value.size() - 1);
	}
	return true;
}


static bool
_ReadFileInt(const std::string& path, int64& result)
{
	std::string text;
	if (!_ReadFileString(path, text) || text.empty())
		return false;

	char* end = NULL;
	long long value = strtoll(text.c_str(), &end, 10);
	if (end == text.c_str())
		return false;

	result = value;
	return true;
}


static status_t
_WriteFileString(const std::string& path, const char* value)
{
	int fd = open(path.c_str(), O_WRONLY | O_TRUNC | O_CLOEXEC);
	if (fd < 0)
		return B_ERROR;

	size_t length = strlen(value);
	ssize_t bytes = write(fd, value, length);
	close(fd);
	return bytes == (ssize_t)length ? B_OK : B_ERROR;
}


static bool
_ListDir(const std::string& path, std::vector<std::string>& entries)
{
	DIR* dir = opendir(path.c_str());
	if (dir == NULL)
		return false;

	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.')
			continue;
		entries.push_back(entry->d_name);
	}
	closedir(dir);
	std::sort(entries.begin(), entries.end());
	return true;
}


static void
_SplitWords(const std::string& text, std::vector<std::string>& words)
{
	size_t start = 0;
	while (start < text.size()) {
		while (start < text.size() && text[start] == ' ')
			start++;
		size_t end = start;
		while (end < text.size() && text[end] != ' ')
			end++;
		if (end > start)
			words.push_back(text.substr(start, end - start));
		start = end;
	}
}


static int
_PopCount(unsigned int value)
{
	int count = 0;
	while (value != 0) {
		count += value & 1;
		value >>= 1;
	}
	return count;
}


//	#pragma mark - generic cpufreq probe


class CpufreqProbe : public OverclockBackend {
public:
							CpufreqProbe(const char* sysfsRoot);

	virtual	const char*		Name() const;
	virtual	bool			IsBootConfig() const;

	virtual	bool			GetCpu(CpufreqState& state);
	virtual	status_t		SetGovernor(const char* governor);
	virtual	status_t		SetFreqRange(int64 minKHz, int64 maxKHz);
	virtual	status_t		SetBoost(bool enable);

	virtual	bool			GetDimms(std::vector<DimmState>& dimms);

protected:
			std::string		_Sysfs(const char* relative) const;
			std::string		_CpuRoot() const;
			std::string		_PolicyRoot() const;
			void			_PolicyDirs(std::vector<std::string>& dirs) const;
			bool			_HasEpp(std::string& availablePath) const;

	std::string				fSysfs;
};


CpufreqProbe::CpufreqProbe(const char* sysfsRoot)
	:
	fSysfs(sysfsRoot != NULL ? sysfsRoot : kDefaultSysfs)
{
}


const char*
CpufreqProbe::Name() const
{
	return "cpufreq";
}


bool
CpufreqProbe::IsBootConfig() const
{
	return false;
}


std::string
CpufreqProbe::_Sysfs(const char* relative) const
{
	return fSysfs + relative;
}


std::string
CpufreqProbe::_CpuRoot() const
{
	return _Sysfs("/devices/system/cpu");
}


std::string
CpufreqProbe::_PolicyRoot() const
{
	return _CpuRoot() + "/cpufreq/policy0";
}


void
CpufreqProbe::_PolicyDirs(std::vector<std::string>& dirs) const
{
	std::string root = _CpuRoot() + "/cpufreq";
	std::vector<std::string> entries;
	if (_ListDir(root, entries)) {
		for (size_t i = 0; i < entries.size(); i++) {
			if (entries[i].compare(0, 6, "policy") == 0)
				dirs.push_back(root + "/" + entries[i]);
		}
	}

	if (dirs.empty()) {
		// Older kernels have no cpufreq/policy*; cpu0 still works.
		std::string cpu0 = _CpuRoot() + "/cpu0/cpufreq";
		if (access((cpu0 + "/scaling_governor").c_str(), F_OK) == 0)
			dirs.push_back(cpu0);
	}
}


bool
CpufreqProbe::_HasEpp(std::string& availablePath) const
{
	std::string base = _PolicyRoot();
	availablePath = base + "/energy_performance_available_preferences";
	if (access(availablePath.c_str(), R_OK) == 0)
		return true;

	availablePath = _CpuRoot() + "/cpu0/cpufreq/"
		"energy_performance_available_preferences";
	return access(availablePath.c_str(), R_OK) == 0;
}


bool
CpufreqProbe::GetCpu(CpufreqState& state)
{
	std::string base = _PolicyRoot();
	if (access((base + "/scaling_governor").c_str(), R_OK) != 0) {
		base = _CpuRoot() + "/cpu0/cpufreq";
		if (access((base + "/scaling_governor").c_str(), R_OK) != 0)
			return false;
	}

	_ReadFileString(base + "/scaling_driver", state.driver);
	_ReadFileString(base + "/scaling_governor", state.governor);

	std::string text;
	if (_ReadFileString(base + "/scaling_available_governors", text))
		_SplitWords(text, state.governors);

	_ReadFileInt(base + "/scaling_cur_freq", state.curKHz);
	_ReadFileInt(base + "/scaling_min_freq", state.minKHz);
	_ReadFileInt(base + "/scaling_max_freq", state.maxKHz);
	_ReadFileInt(base + "/cpuinfo_min_freq", state.hwMinKHz);
	_ReadFileInt(base + "/cpuinfo_max_freq", state.hwMaxKHz);

	std::string boostPath = _CpuRoot() + "/cpufreq/boost";
	state.boostAvailable = access(boostPath.c_str(), F_OK) == 0;
	if (state.boostAvailable) {
		std::string boost;
		state.boostOn = _ReadFileString(boostPath, boost)
			&& boost == "1";
	}

	std::string availablePath;
	if (_HasEpp(availablePath)) {
		_ReadFileString(availablePath, text);
		_SplitWords(text, state.eppPreferences);
		_ReadFileString(base + "/energy_performance_preference",
			state.epp);
		if (state.epp.empty()) {
			_ReadFileString(_CpuRoot()
				+ "/cpu0/cpufreq/energy_performance_preference",
				state.epp);
		}
	}

	return true;
}


status_t
CpufreqProbe::SetGovernor(const char* governor)
{
	if (governor == NULL || governor[0] == '\0')
		return B_BAD_VALUE;

	std::vector<std::string> dirs;
	_PolicyDirs(dirs);
	if (dirs.empty())
		return B_UNSUPPORTED;

	status_t result = B_ERROR;
	for (size_t i = 0; i < dirs.size(); i++) {
		std::string text;
		std::vector<std::string> available;
		if (_ReadFileString(dirs[i] + "/scaling_available_governors",
				text)) {
			_SplitWords(text, available);
			if (std::find(available.begin(), available.end(),
					governor) == available.end()) {
				continue;
			}
		}

		if (_WriteFileString(dirs[i] + "/scaling_governor",
				governor) == B_OK) {
			result = B_OK;
		}
	}
	return result;
}


status_t
CpufreqProbe::SetFreqRange(int64 minKHz, int64 maxKHz)
{
	if (minKHz < 0 || maxKHz < 0 || minKHz > maxKHz)
		return B_BAD_VALUE;

	std::vector<std::string> dirs;
	_PolicyDirs(dirs);
	if (dirs.empty())
		return B_UNSUPPORTED;

	status_t result = B_ERROR;
	for (size_t i = 0; i < dirs.size(); i++) {
		int64 hwMin = -1;
		int64 hwMax = -1;
		_ReadFileInt(dirs[i] + "/cpuinfo_min_freq", hwMin);
		_ReadFileInt(dirs[i] + "/cpuinfo_max_freq", hwMax);
		if (hwMin >= 0 && minKHz < hwMin)
			minKHz = hwMin;
		if (hwMax >= 0 && maxKHz > hwMax)
			maxKHz = hwMax;
		if (minKHz > maxKHz)
			return B_BAD_VALUE;

		int64 curMin = -1;
		_ReadFileInt(dirs[i] + "/scaling_min_freq", curMin);

		// Lowering the ceiling below the current floor is rejected by
		// some kernels, so write the side that opens room first.
		char value[32];
		bool maxFirst = curMin < 0 || maxKHz >= curMin;
		snprintf(value, sizeof(value), "%lld", (long long)maxKHz);
		status_t maxStatus = maxFirst
			? _WriteFileString(dirs[i] + "/scaling_max_freq", value)
			: B_ERROR;
		snprintf(value, sizeof(value), "%lld", (long long)minKHz);
		status_t minStatus = _WriteFileString(dirs[i]
			+ "/scaling_min_freq", value);
		if (!maxFirst) {
			snprintf(value, sizeof(value), "%lld",
				(long long)maxKHz);
			maxStatus = _WriteFileString(dirs[i]
				+ "/scaling_max_freq", value);
		}

		if (minStatus == B_OK && maxStatus == B_OK)
			result = B_OK;
	}
	return result;
}


status_t
CpufreqProbe::SetBoost(bool enable)
{
	std::string boostPath = _CpuRoot() + "/cpufreq/boost";
	if (access(boostPath.c_str(), W_OK) != 0)
		return B_UNSUPPORTED;

	return _WriteFileString(boostPath, enable ? "1" : "0");
}


bool
CpufreqProbe::GetDimms(std::vector<DimmState>& dimms)
{
	std::vector<std::string> buses;
	if (!_ListDir(_Sysfs("/bus/i2c/devices"), buses))
		return false;

	for (size_t i = 0; i < buses.size(); i++) {
		const std::string& name = buses[i];
		size_t length = name.size();
		if (length < 5 || name[length - 5] != '-')
			continue;
		if (name.compare(length - 4, 4, "0050") != 0
			&& name.compare(length - 4, 4, "0051") != 0
			&& name.compare(length - 4, 4, "0052") != 0
			&& name.compare(length - 4, 4, "0053") != 0) {
			continue;
		}

		std::string eepromPath = _Sysfs("/bus/i2c/devices/") + name
			+ "/eeprom";
		int fd = open(eepromPath.c_str(), O_RDONLY | O_CLOEXEC);
		if (fd < 0)
			continue;

		uint8 buffer[1024];
		ssize_t bytes = 0;
		while (bytes < (ssize_t)sizeof(buffer)) {
			ssize_t readBytes = read(fd, buffer + bytes,
				sizeof(buffer) - bytes);
			if (readBytes <= 0)
				break;
			bytes += readBytes;
		}
		close(fd);
		if (bytes < 128)
			continue;

		DimmState dimm;
		dimm.slot = name[length - 1] - '0';
		if (DecodeSpd(buffer, bytes, dimm) == B_OK)
			dimm.decoded = true;
		dimms.push_back(dimm);
	}
	return !dimms.empty();
}


//	#pragma mark - Intel probe (intel_pstate + RAPL powercap)


class IntelProbe : public CpufreqProbe {
public:
							IntelProbe(const char* sysfsRoot);

	virtual	const char*		Name() const;
	virtual	bool			GetCpu(CpufreqState& state);
	virtual	status_t		SetBoost(bool enable);

	virtual	bool			GetRapl(RaplState& state);
	virtual	status_t		SetPowerLimit(int64 longUw, int64 shortUw);

private:
			bool			_FindPackageZone(std::string& zonePath,
									std::string& name) const;
};


IntelProbe::IntelProbe(const char* sysfsRoot)
	:
	CpufreqProbe(sysfsRoot)
{
}


const char*
IntelProbe::Name() const
{
	return "intel";
}


bool
IntelProbe::GetCpu(CpufreqState& state)
{
	if (!CpufreqProbe::GetCpu(state))
		return false;

	// intel_pstate in active mode has no cpufreq/boost file; the
	// driver exposes the inverted switch instead.
	std::string noTurboPath = _CpuRoot() + "/intel_pstate/no_turbo";
	std::string noTurbo;
	if (_ReadFileString(noTurboPath, noTurbo)) {
		state.boostAvailable = true;
		state.boostOn = noTurbo == "0";
	}
	return true;
}


status_t
IntelProbe::SetBoost(bool enable)
{
	std::string noTurboPath = _CpuRoot() + "/intel_pstate/no_turbo";
	if (access(noTurboPath.c_str(), W_OK) == 0)
		return _WriteFileString(noTurboPath, enable ? "0" : "1");

	return CpufreqProbe::SetBoost(enable);
}


bool
IntelProbe::_FindPackageZone(std::string& zonePath, std::string& name) const
{
	static const char kRaplPrefix[] = "intel-rapl:";
	static const char kPackagePrefix[] = "package-";

	std::vector<std::string> zones;
	if (!_ListDir(_Sysfs("/class/powercap"), zones))
		return false;

	for (size_t i = 0; i < zones.size(); i++) {
		if (zones[i].compare(0, strlen(kRaplPrefix), kRaplPrefix)
				!= 0) {
			continue;
		}
		std::string zoneName;
		if (!_ReadFileString(_Sysfs("/class/powercap/") + zones[i]
				+ "/name", zoneName)) {
			continue;
		}
		if (zoneName.compare(0, strlen(kPackagePrefix), kPackagePrefix)
				== 0) {
			zonePath = _Sysfs("/class/powercap/") + zones[i] + "/";
			name = zoneName;
			return true;
		}
	}
	return false;
}


bool
IntelProbe::GetRapl(RaplState& state)
{
	std::string zonePath;
	if (!_FindPackageZone(zonePath, state.name))
		return false;

	_ReadFileInt(zonePath + "constraint_0_power_limit_uw",
		state.curLongUw);
	_ReadFileInt(zonePath + "constraint_0_max_power_uw",
		state.maxLongUw);

	std::string shortName;
	if (_ReadFileString(zonePath + "constraint_1_name", shortName)
		&& shortName == "short_term") {
		_ReadFileInt(zonePath + "constraint_1_power_limit_uw",
			state.curShortUw);
		_ReadFileInt(zonePath + "constraint_1_max_power_uw",
			state.maxShortUw);
	}

	state.available = state.curLongUw >= 0 && state.maxLongUw > 0;
	return state.available;
}


status_t
IntelProbe::SetPowerLimit(int64 longUw, int64 shortUw)
{
	std::string zonePath;
	std::string zoneName;
	if (!_FindPackageZone(zonePath, zoneName))
		return B_UNSUPPORTED;

	RaplState rapl;
	if (!GetRapl(rapl))
		return B_UNSUPPORTED;

	status_t result = B_OK;
	char value[32];

	if (longUw >= 0) {
		if (longUw > rapl.maxLongUw)
			longUw = rapl.maxLongUw;
		snprintf(value, sizeof(value), "%lld", (long long)longUw);
		result = _WriteFileString(zonePath
			+ "constraint_0_power_limit_uw", value);
	}

	if (shortUw >= 0 && rapl.curShortUw >= 0 && rapl.maxShortUw > 0) {
		if (shortUw > rapl.maxShortUw)
			shortUw = rapl.maxShortUw;
		snprintf(value, sizeof(value), "%lld", (long long)shortUw);
		if (_WriteFileString(zonePath + "constraint_1_power_limit_uw",
				value) != B_OK) {
			result = B_ERROR;
		}
	}

	return result;
}


//	#pragma mark - AMD probe (amd_pstate + EPP)


class AmdProbe : public CpufreqProbe {
public:
							AmdProbe(const char* sysfsRoot);

	virtual	const char*		Name() const;

	virtual	bool			GetAmdPstate(AmdPstateState& state);
	virtual	status_t		SetEpp(const char* preference);
};


AmdProbe::AmdProbe(const char* sysfsRoot)
	:
	CpufreqProbe(sysfsRoot)
{
}


const char*
AmdProbe::Name() const
{
	return "amd-pstate";
}


bool
AmdProbe::GetAmdPstate(AmdPstateState& state)
{
	std::string status;
	if (!_ReadFileString(_CpuRoot() + "/amd_pstate/status", status))
		return false;

	state.available = true;
	state.status = status;

	std::string prefCore;
	if (_ReadFileString(_CpuRoot() + "/amd_pstate/hw_prefcore",
			prefCore)) {
		state.prefCoreAvailable = true;
		state.prefCoreEnabled = prefCore == "Y" || prefCore == "1";
	}
	return true;
}


status_t
AmdProbe::SetEpp(const char* preference)
{
	if (preference == NULL || preference[0] == '\0')
		return B_BAD_VALUE;

	std::vector<std::string> dirs;
	_PolicyDirs(dirs);
	if (dirs.empty())
		return B_UNSUPPORTED;

	status_t result = B_ERROR;
	for (size_t i = 0; i < dirs.size(); i++) {
		std::string availablePath = dirs[i]
			+ "/energy_performance_available_preferences";
		std::string text;
		std::vector<std::string> available;
		if (!_ReadFileString(availablePath, text))
			continue;
		_SplitWords(text, available);
		if (std::find(available.begin(), available.end(), preference)
				== available.end()) {
			continue;
		}
		if (_WriteFileString(dirs[i]
				+ "/energy_performance_preference",
				preference) == B_OK) {
			result = B_OK;
		}
	}
	return result;
}


//	#pragma mark - Raspberry Pi probe (config.txt)


class RpiProbe : public CpufreqProbe {
public:
							RpiProbe(const char* sysfsRoot,
								const char* bootRoot);

	virtual	const char*		Name() const;
	virtual	bool			IsBootConfig() const;

	virtual	bool			GetRpi(RpiOverclockState& state);
	virtual	status_t		SetRpi(const RpiOverclockState& state);

private:
			std::string		_ConfigPath() const;
			bool			_GetKey(const char* key, int& value) const;
			void			_RemoveBlock(
								std::vector<std::string>& lines) const;

	std::string				fBoot;
};


RpiProbe::RpiProbe(const char* sysfsRoot, const char* bootRoot)
	:
	CpufreqProbe(sysfsRoot),
	fBoot(bootRoot != NULL ? bootRoot : kDefaultBoot)
{
}


const char*
RpiProbe::Name() const
{
	return "rpi-config";
}


bool
RpiProbe::IsBootConfig() const
{
	return true;
}


std::string
RpiProbe::_ConfigPath() const
{
	std::string firmware = fBoot + "/firmware/config.txt";
	if (access(firmware.c_str(), F_OK) == 0)
		return firmware;
	return fBoot + "/config.txt";
}


bool
RpiProbe::_GetKey(const char* key, int& value) const
{
	FILE* file = fopen(_ConfigPath().c_str(), "r");
	if (file == NULL)
		return false;

	// Last assignment wins so the managed block at the end of the
	// file overrides anything the image shipped.
	bool found = false;
	char line[256];
	size_t keyLength = strlen(key);
	while (fgets(line, sizeof(line), file) != NULL) {
		if (strncmp(line, key, keyLength) != 0
			|| line[keyLength] != '=') {
			continue;
		}
		int parsed = atoi(line + keyLength + 1);
		value = parsed;
		found = true;
	}
	fclose(file);
	return found;
}


void
RpiProbe::_RemoveBlock(std::vector<std::string>& lines) const
{
	bool inBlock = false;
	for (size_t i = 0; i < lines.size();) {
		if (lines[i] == kRpiBlockBegin)
			inBlock = true;
		if (inBlock) {
			bool isEnd = lines[i] == kRpiBlockEnd;
			lines.erase(lines.begin() + i);
			if (isEnd)
				inBlock = false;
			continue;
		}
		i++;
	}
}


bool
RpiProbe::GetRpi(RpiOverclockState& state)
{
	if (access(_ConfigPath().c_str(), R_OK) != 0)
		return false;

	int value;
	state.hasArmFreq = _GetKey("arm_freq", value);
	if (state.hasArmFreq)
		state.armFreq = value;
	state.hasOverVoltage = _GetKey("over_voltage", value);
	if (state.hasOverVoltage)
		state.overVoltage = value;
	state.hasSdramFreq = _GetKey("sdram_freq", value);
	if (state.hasSdramFreq)
		state.sdramFreq = value;
	state.hasGpuFreq = _GetKey("gpu_freq", value);
	if (state.hasGpuFreq)
		state.gpuFreq = value;
	return true;
}


status_t
RpiProbe::SetRpi(const RpiOverclockState& state)
{
	std::string path = _ConfigPath();
	FILE* file = fopen(path.c_str(), "r");
	if (file == NULL)
		return B_ERROR;

	std::vector<std::string> lines;
	char line[256];
	while (fgets(line, sizeof(line), file) != NULL) {
		std::string text(line);
		while (!text.empty() && text[text.size() - 1] == '\n')
			text.erase(text.size() - 1);
		lines.push_back(text);
	}
	fclose(file);
	_RemoveBlock(lines);

	bool any = state.hasArmFreq || state.hasOverVoltage
		|| state.hasSdramFreq || state.hasGpuFreq;
	if (any) {
		char buffer[64];
		lines.push_back(kRpiBlockBegin);
		if (state.hasArmFreq) {
			snprintf(buffer, sizeof(buffer), "arm_freq=%d",
				state.armFreq);
			lines.push_back(buffer);
		}
		if (state.hasOverVoltage) {
			snprintf(buffer, sizeof(buffer), "over_voltage=%d",
				state.overVoltage);
			lines.push_back(buffer);
		}
		if (state.hasSdramFreq) {
			snprintf(buffer, sizeof(buffer), "sdram_freq=%d",
				state.sdramFreq);
			lines.push_back(buffer);
		}
		if (state.hasGpuFreq) {
			snprintf(buffer, sizeof(buffer), "gpu_freq=%d",
				state.gpuFreq);
			lines.push_back(buffer);
		}
		lines.push_back(kRpiBlockEnd);
	}

	file = fopen(path.c_str(), "w");
	if (file == NULL)
		return B_ERROR;
	for (size_t i = 0; i < lines.size(); i++)
		fprintf(file, "%s\n", lines[i].c_str());
	fclose(file);
	return B_OK;
}


//	#pragma mark - SPD decoding


static float
_Ddr4TimingNs(uint8 mtb, uint8 ftb)
{
	// DDR4: medium timebase 125 ps, fine timebase 1 ps (signed).
	return ((int)mtb * 125 + (int)(int8)ftb) / 1000.0f;
}


/*static*/ status_t
OverclockBackend::DecodeSpd(const uint8* data, size_t length,
	DimmState& dimm)
{
	if (data == NULL || length < 128)
		return B_BAD_VALUE;

	dimm.xmp = false;
	dimm.xmpProfiles = 0;

	switch (data[2]) {
		case 0x0b:	// DDR3 SDRAM
		{
			dimm.type = "DDR3";
			int exponent = (data[3] & 0x0f) + (data[4] & 0x0f) - 17;
			int banks = data[5] * data[17];
			if (exponent > 0 && exponent <= 12 && banks > 0)
				dimm.sizeMB = (1LL << exponent) * banks;

			float ctime = (data[9] >> 4) + (data[9] & 0x0f) * 0.1f;
			if (ctime > 0)
				dimm.maxSpeedMTs = (int)(2000.0f / ctime + 0.5f);

			if (length > 177 && data[176] == 0x0c
				&& data[177] == 0x4a) {
				dimm.xmp = true;
				dimm.xmpProfiles = _PopCount(data[178] & 0x03);
			}
			break;
		}
		case 0x0c:	// DDR4 SDRAM
		{
			dimm.type = "DDR4";
			int sdramWidth = 4 << (data[12] & 0x07);
			int ranks = ((data[12] >> 3) & 0x07) + 1;
			int signalLoading = data[6] & 0x03;
			int dieCount = ((data[6] >> 4) & 0x07) + 1;
			long long capacity = (256LL << (data[4] & 0x0f)) / 8;
			capacity *= (8 << (data[13] & 0x07)) / sdramWidth;
			capacity *= ranks;
			if (signalLoading == 0x02)
				capacity *= dieCount;
			dimm.sizeMB = capacity;

			float ctime = _Ddr4TimingNs(data[18], data[125]);
			if (ctime > 0)
				dimm.maxSpeedMTs = (int)(2000.0f / ctime + 0.5f);

			if (length > 386 && data[384] == 0x0c
				&& data[385] == 0x4a) {
				dimm.xmp = true;
				dimm.xmpProfiles = _PopCount(data[386] & 0x03);
			}
			// AMD EXPO stores its own profile block separately.
			if (length > 771 && data[768] == 0x45
				&& data[769] == 0x58 && data[770] == 0x50
				&& data[771] == 0x4f) {
				dimm.xmp = true;
				if (dimm.xmpProfiles == 0)
					dimm.xmpProfiles = 2;
			}
			break;
		}
		case 0x12:	// DDR5 SDRAM
		{
			// DDR5 module sizing varies by module architecture;
			// decode only what several decoders agree on.
			dimm.type = "DDR5";
			if (length > 386 && data[384] == 0x0c
				&& data[385] == 0x4a) {
				dimm.xmp = true;
				dimm.xmpProfiles = _PopCount(data[386] & 0x1f);
			}
			if (length > 771 && data[768] == 0x45
				&& data[769] == 0x58 && data[770] == 0x50
				&& data[771] == 0x4f) {
				dimm.xmp = true;
				if (dimm.xmpProfiles == 0)
					dimm.xmpProfiles = 2;
			}
			break;
		}
		default:
			dimm.type = "unknown";
			return B_UNSUPPORTED;
	}

	dimm.decoded = true;
	return B_OK;
}


//	#pragma mark - defaults and probe order


OverclockBackend::OverclockBackend()
{
}


OverclockBackend::~OverclockBackend()
{
}


bool
OverclockBackend::GetRapl(RaplState& /*state*/)
{
	return false;
}


status_t
OverclockBackend::SetPowerLimit(int64 /*longUw*/, int64 /*shortUw*/)
{
	return B_UNSUPPORTED;
}


bool
OverclockBackend::GetAmdPstate(AmdPstateState& /*state*/)
{
	return false;
}


status_t
OverclockBackend::SetEpp(const char* /*preference*/)
{
	return B_UNSUPPORTED;
}


bool
OverclockBackend::GetRpi(RpiOverclockState& /*state*/)
{
	return false;
}


status_t
OverclockBackend::SetRpi(const RpiOverclockState& /*state*/)
{
	return B_UNSUPPORTED;
}


bool
OverclockBackend::GetDimms(std::vector<DimmState>& /*dimms*/)
{
	// Every concrete probe overrides this with its sysfs root.
	return false;
}


/*static*/ OverclockBackend*
OverclockBackend::Create(const char* sysfsRoot, const char* bootRoot)
{
	const char* sysfs = sysfsRoot != NULL ? sysfsRoot : kDefaultSysfs;
	const char* boot = bootRoot != NULL ? bootRoot : kDefaultBoot;

	// Raspberry Pi images carry config.txt; nothing else does.
	std::string firmware = std::string(boot) + "/firmware/config.txt";
	std::string legacy = std::string(boot) + "/config.txt";
	if (access(firmware.c_str(), F_OK) == 0
		|| access(legacy.c_str(), F_OK) == 0) {
		return new RpiProbe(sysfs, boot);
	}

	std::string cpuRoot = std::string(sysfs) + "/devices/system/cpu";
	std::string vendorPath = cpuRoot + "/cpu0/cpufreq/scaling_driver";
	std::string driver;
	if (!_ReadFileString(vendorPath, driver)) {
		// No cpufreq at all (VM without cpufreq, some SBCs): still
		// report a backend so the tab can show what is missing.
		return new CpufreqProbe(sysfs);
	}

	if (driver.find("amd") != std::string::npos)
		return new AmdProbe(sysfs);
	if (driver.find("intel") != std::string::npos)
		return new IntelProbe(sysfs);

	// Fall back on the CPU vendor when the driver name is generic
	// (e.g. "acpi-cpufreq" on both vendors).
	std::string vendorId;
	FILE* file = fopen("/proc/cpuinfo", "r");
	if (file != NULL) {
		char line[256];
		while (fgets(line, sizeof(line), file) != NULL) {
			if (strncmp(line, "vendor_id", 9) == 0) {
				if (strstr(line, "AuthenticAMD") != NULL)
					vendorId = "AuthenticAMD";
				else if (strstr(line, "GenuineIntel") != NULL)
					vendorId = "GenuineIntel";
				break;
			}
		}
		fclose(file);
	}

	if (vendorId == "AuthenticAMD")
		return new AmdProbe(sysfs);
	if (vendorId == "GenuineIntel")
		return new IntelProbe(sysfs);
	return new CpufreqProbe(sysfs);
}
