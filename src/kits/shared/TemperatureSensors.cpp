/*
 * Copyright 2026, Vitruvian OS contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "TemperatureSensors.h"

#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>


const char* TemperatureSensorReader::kDefaultRoot = "/sys";


//	#pragma mark - Helpers


static std::string
_ReadTrimmedFile(const std::string& path)
{
	int fd = open(path.c_str(), O_RDONLY);
	if (fd < 0)
		return std::string();

	char buffer[256];
	ssize_t bytesRead = read(fd, buffer, sizeof(buffer) - 1);
	close(fd);
	if (bytesRead <= 0)
		return std::string();

	buffer[bytesRead] = '\0';
	std::string value(buffer);
	while (!value.empty()) {
		char c = value[value.size() - 1];
		if (c == '\n' || c == '\r' || c == ' ' || c == '\t')
			value.resize(value.size() - 1);
		else
			break;
	}
	return value;
}


static float
_ReadMilliValueFile(const std::string& path)
{
	std::string content = _ReadTrimmedFile(path);
	if (content.empty())
		return NAN;

	char* end = NULL;
	long long value = strtoll(content.c_str(), &end, 10);
	if (end == content.c_str())
		return NAN;

	// all Linux thermal sysfs values are millidegrees Celsius
	return (float)value / 1000.0f;
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


//! Matches "temp<N>_input"; returns the "temp<N>" base name on success.
static bool
_MatchTempInput(const char* name, std::string& baseName)
{
	const char* prefix = "temp";
	if (strncmp(name, prefix, strlen(prefix)) != 0)
		return false;

	const char* digits = name + strlen(prefix);
	const char* suffix = strstr(digits, "_input");
	if (suffix == NULL || strcmp(suffix, "_input") != 0)
		return false;
	if (suffix == digits)
		return false;

	for (const char* c = digits; c < suffix; c++) {
		if (*c < '0' || *c > '9')
			return false;
	}

	baseName.assign(name, suffix - name);
	return true;
}


//	#pragma mark - TemperatureSensor


std::string
TemperatureSensor::DisplayName() const
{
	if (chip.empty())
		return label;
	if (label.empty() || label == chip)
		return chip;
	return chip + ": " + label;
}


//	#pragma mark - TemperatureSensorReader


TemperatureSensorReader::TemperatureSensorReader(const char* root)
{
	if (root != NULL && root[0] != '\0')
		fRoot = root;
	else
		fRoot = kDefaultRoot;

	Rescan();
}


int
TemperatureSensorReader::Rescan()
{
	fSensors.clear();

	_ScanHwmon();
	// thermal_zone is the fallback: on hwmon-capable kernels it usually
	// duplicates the same chips, so only use it when hwmon had none.
	if (fSensors.empty())
		_ScanThermalZones();

	return (int)fSensors.size();
}


void
TemperatureSensorReader::_ScanHwmon()
{
	std::vector<std::string> chips;
	if (!_ListDir(fRoot + "/class/hwmon", chips))
		return;

	for (size_t i = 0; i < chips.size(); i++) {
		if (chips[i].compare(0, 5, "hwmon") != 0)
			continue;

		std::string chipPath = fRoot + "/class/hwmon/" + chips[i];
		std::string chipName = _ReadTrimmedFile(chipPath + "/name");
		if (chipName.empty())
			chipName = chips[i];

		std::vector<std::string> files;
		if (!_ListDir(chipPath, files))
			continue;

		for (size_t j = 0; j < files.size(); j++) {
			std::string baseName;
			if (!_MatchTempInput(files[j].c_str(), baseName))
				continue;

			TemperatureSensor sensor;
			sensor.isHwmon = true;
			sensor.chip = chipName;
			sensor.valuePath = chipPath + "/" + files[j];

			sensor.label = _ReadTrimmedFile(
				chipPath + "/" + baseName + "_label");
			if (sensor.label.empty())
				sensor.label = baseName;

			sensor.limitPath = chipPath + "/" + baseName + "_crit";
			int fd = open(sensor.limitPath.c_str(), O_RDONLY);
			if (fd < 0) {
				sensor.limitPath = chipPath + "/" + baseName + "_max";
				fd = open(sensor.limitPath.c_str(), O_RDONLY);
			}
			if (fd < 0)
				sensor.limitPath = std::string();
			else
				close(fd);

			sensor.id = "hwmon:" + chipName + ":" + baseName;
			// disambiguate identical chip names (e.g. two NVMe drives)
			for (size_t k = 0; k < fSensors.size(); k++) {
				if (fSensors[k].id == sensor.id) {
					char suffix[24];
					snprintf(suffix, sizeof(suffix), "#%zu", k);
					sensor.id += suffix;
					break;
				}
			}

			fSensors.push_back(sensor);
		}
	}
}


void
TemperatureSensorReader::_ScanThermalZones()
{
	std::vector<std::string> zones;
	if (!_ListDir(fRoot + "/class/thermal", zones))
		return;

	for (size_t i = 0; i < zones.size(); i++) {
		if (zones[i].compare(0, 12, "thermal_zone") != 0)
			continue;

		std::string zonePath = fRoot + "/class/thermal/" + zones[i];

		TemperatureSensor sensor;
		sensor.isHwmon = false;
		sensor.valuePath = zonePath + "/temp";
		int fd = open(sensor.valuePath.c_str(), O_RDONLY);
		if (fd < 0)
			continue;
		close(fd);

		sensor.label = _ReadTrimmedFile(zonePath + "/type");
		if (sensor.label.empty())
			sensor.label = zones[i];
		sensor.id = "thermal:" + zones[i];

		fSensors.push_back(sensor);
	}
}


const TemperatureSensor*
TemperatureSensorReader::FindSensor(const std::string& id) const
{
	for (size_t i = 0; i < fSensors.size(); i++) {
		if (fSensors[i].id == id)
			return &fSensors[i];
	}

	return NULL;
}


float
TemperatureSensorReader::ReadCelsius(const std::string& id) const
{
	const TemperatureSensor* sensor = FindSensor(id);
	if (sensor == NULL)
		return NAN;

	return ReadCelsius(*sensor);
}


float
TemperatureSensorReader::ReadCelsius(const TemperatureSensor& sensor) const
{
	return _ReadMilliValueFile(sensor.valuePath);
}


float
TemperatureSensorReader::ReadLimitCelsius(const std::string& id) const
{
	const TemperatureSensor* sensor = FindSensor(id);
	if (sensor == NULL || sensor->limitPath.empty())
		return NAN;

	return _ReadMilliValueFile(sensor->limitPath);
}
