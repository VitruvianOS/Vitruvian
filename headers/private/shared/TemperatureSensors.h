/*
 * Copyright 2026, Vitruvian OS contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef TEMPERATURE_SENSORS_H
#define TEMPERATURE_SENSORS_H


#include <string>
#include <vector>


//! A single temperature sensor discovered in sysfs.
class TemperatureSensor {
public:
	std::string	id;			// stable identifier, e.g. "hwmon:coretemp:temp1"
	std::string	chip;		// hwmon chip name ("coretemp", "k10temp", ...); empty for thermal zones
	std::string	label;		// human readable label ("Package id 0", "x86_pkg_temp", ...)
	std::string	valuePath;	// file holding the current temperature (millidegrees Celsius)
	std::string	limitPath;	// file holding temp*_crit or temp*_max; empty if none
	bool		isHwmon;	// true: class/hwmon, false: class/thermal fallback

			std::string	DisplayName() const;
};


/*!
 * Discovers and reads Linux temperature sensors:
 * sysfs class/hwmon temp*_input attributes (with temp*_label and temp*_crit
 * or temp*_max), falling back to class/thermal thermal zones when hwmon
 * exposes none.
 *
 * The sysfs root is configurable so tests can point the reader at a fake
 * tree. Discovery runs once (on construction or Rescan()); reading is cheap
 * and can be done on every refresh. When no sensor exists, the sensor list
 * stays empty -- callers must not invent fake values.
 */
class TemperatureSensorReader {
public:
	static const char*	kDefaultRoot;

						TemperatureSensorReader(const char* root = NULL);

	const	std::vector<TemperatureSensor>& Sensors() const { return fSensors; }

			//! Re-runs discovery; returns the number of sensors found.
			int			Rescan();

	const	TemperatureSensor* FindSensor(const std::string& id) const;

			//! Current temperature in Celsius, or NAN if it cannot be read.
			float		ReadCelsius(const std::string& id) const;
			float		ReadCelsius(const TemperatureSensor& sensor) const;

			//! Critical/max limit in Celsius, or NAN if the sensor has none.
			float		ReadLimitCelsius(const std::string& id) const;

private:
			void		_ScanHwmon();
			void		_ScanThermalZones();

	std::string			fRoot;
	std::vector<TemperatureSensor>	fSensors;
};


#endif	// TEMPERATURE_SENSORS_H
