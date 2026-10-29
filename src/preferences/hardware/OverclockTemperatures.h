/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _OVERCLOCK_TEMPERATURES_H
#define _OVERCLOCK_TEMPERATURES_H


#include <TemperatureSensors.h>

#include <string>


enum oc_temp_kind {
	OC_TEMP_CPU,
	OC_TEMP_GPU,
	OC_TEMP_MEMORY
};


//! Best sensor for a section, or NULL when the machine has none.
const TemperatureSensor*	OverclockFindSensor(const TemperatureSensorReader& reader,
								oc_temp_kind kind);

//! "45.2 C" style reading with degree sign, empty when unreadable.
std::string				OverclockFormatCelsius(TemperatureSensorReader& reader,
								const TemperatureSensor* sensor);

//! "CPU 45.2 C \xb7 GPU 38.0 C" style line, empty when no sensors.
std::string				OverclockTemperatureSummary(
								TemperatureSensorReader& reader);


#endif	// _OVERCLOCK_TEMPERATURES_H
