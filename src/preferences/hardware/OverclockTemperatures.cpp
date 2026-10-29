/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "OverclockTemperatures.h"

#include <stdio.h>
#include <string.h>


static bool
_Contains(const std::string& text, const char* needle)
{
	return text.find(needle) != std::string::npos;
}


static bool
_ChipIn(const std::string& chip, const char* const* chips, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		if (chip == chips[i])
			return true;
	}
	return false;
}


const TemperatureSensor*
OverclockFindSensor(const TemperatureSensorReader& reader, oc_temp_kind kind)
{
	static const char* kCpuChips[] = {
		"coretemp", "k10temp", "zenpower", "cpu_thermal"
	};
	static const char* kGpuChips[] = {
		"amdgpu", "nouveau", "radeon", "i915", "xe", "v3d", "panfrost",
		"mali"
	};
	static const char* kMemoryChips[] = {
		"soc_dram", "ddr", "dram"
	};

	const std::vector<TemperatureSensor>& sensors = reader.Sensors();
	for (size_t i = 0; i < sensors.size(); i++) {
		const TemperatureSensor& sensor = sensors[i];
		switch (kind) {
			case OC_TEMP_CPU:
				if (_ChipIn(sensor.chip, kCpuChips,
						sizeof(kCpuChips) / sizeof(char*))) {
					return &sensor;
				}
				if (_Contains(sensor.label, "Package")
					|| _Contains(sensor.label, "x86_pkg")
					|| _Contains(sensor.label, "cpu-thermal")) {
					return &sensor;
				}
				break;
			case OC_TEMP_GPU:
				if (_ChipIn(sensor.chip, kGpuChips,
						sizeof(kGpuChips) / sizeof(char*))) {
					return &sensor;
				}
				if (_Contains(sensor.label, "gpu")) {
					return &sensor;
				}
				break;
			case OC_TEMP_MEMORY:
				if (_ChipIn(sensor.chip, kMemoryChips,
						sizeof(kMemoryChips) / sizeof(char*))) {
					return &sensor;
				}
				if (_Contains(sensor.label, "dram")
					|| _Contains(sensor.label, "dimm")
					|| _Contains(sensor.label, "mem")) {
					return &sensor;
				}
				break;
		}
	}
	return NULL;
}


std::string
OverclockFormatCelsius(TemperatureSensorReader& reader,
	const TemperatureSensor* sensor)
{
	if (sensor == NULL)
		return std::string();

	float celsius = reader.ReadCelsius(*sensor);
	if (celsius != celsius)	// NAN: sensor vanished or read failed.
		return std::string();

	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%.1f \xc2\xb0"
		"C", celsius);
	return buffer;
}


std::string
OverclockTemperatureSummary(TemperatureSensorReader& reader)
{
	struct {
		oc_temp_kind	kind;
		const char*		label;
	} parts[] = {
		{ OC_TEMP_CPU, "CPU" },
		{ OC_TEMP_GPU, "GPU" },
		{ OC_TEMP_MEMORY, "RAM" }
	};

	std::string summary;
	for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
		std::string value = OverclockFormatCelsius(reader,
			OverclockFindSensor(reader, parts[i].kind));
		if (value.empty())
			continue;
		if (!summary.empty())
			summary += " \xc2\xb7 ";
		summary += parts[i].label;
		summary += " ";
		summary += value;
	}
	return summary;
}
