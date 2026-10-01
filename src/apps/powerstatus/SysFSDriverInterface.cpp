/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "SysFSDriverInterface.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <new>

#include <Autolock.h>
#include <Directory.h>
#include <Entry.h>
#include <Path.h>


static const char* kSysfsPowerSupply = "/sys/class/power_supply";


//	#pragma mark - Helper: read sysfs attributes


status_t
SysFSDriverInterface::_ReadIntAttr(const char* path, const char* attr,
	int32* value)
{
	BString fullPath(path);
	fullPath << "/" << attr;

	int fd = open(fullPath.String(), O_RDONLY);
	if (fd < 0)
		return errno;

	char buf[32];
	ssize_t bytesRead = read(fd, buf, sizeof(buf) - 1);
	close(fd);

	if (bytesRead <= 0)
		return B_ERROR;

	buf[bytesRead] = '\0';

	// Strip trailing whitespace/newlines
	for (ssize_t i = bytesRead - 1; i >= 0; i--) {
		if (buf[i] == '\n' || buf[i] == '\r' || buf[i] == ' ')
			buf[i] = '\0';
		else
			break;
	}

	if (strlen(buf) == 0)
		return B_ERROR;

	*value = atoi(buf);
	return B_OK;
}


status_t
SysFSDriverInterface::_ReadStringAttr(const char* path, const char* attr,
	BString* value)
{
	BString fullPath(path);
	fullPath << "/" << attr;

	int fd = open(fullPath.String(), O_RDONLY);
	if (fd < 0)
		return errno;

	char buf[128];
	ssize_t bytesRead = read(fd, buf, sizeof(buf) - 1);
	close(fd);

	if (bytesRead <= 0)
		return B_ERROR;

	buf[bytesRead] = '\0';

	// Strip trailing whitespace/newlines
	for (ssize_t i = bytesRead - 1; i >= 0; i--) {
		if (buf[i] == '\n' || buf[i] == '\r' || buf[i] == ' ')
			buf[i] = '\0';
		else
			break;
	}

	value->SetTo(buf);
	return B_OK;
}


//	#pragma mark - Battery state interpretation


status_t
SysFSDriverInterface::_ReadBatteryState(sysfs_battery* battery)
{
	BString statusStr;
	status_t status = _ReadStringAttr(battery->path.String(), "status",
		&statusStr);
	if (status != B_OK) {
		// Some batteries don't have a status file when fully charged
		// or when the adapter is plugged in. Check online attribute.
		int32 online = 0;
		status = _ReadIntAttr(battery->path.String(), "online", &online);
		if (status == B_OK && online) {
			battery->state = 0; // Not discharging, not charging
		} else {
			battery->state = BATTERY_CRITICAL_STATE;
		}
		return B_OK;
	}

	if (statusStr.ICompare("Charging") == 0)
		battery->state = BATTERY_CHARGING;
	else if (statusStr.ICompare("Discharging") == 0)
		battery->state = BATTERY_DISCHARGING;
	else if (statusStr.ICompare("Full") == 0)
		battery->state = 0; // Fully charged, not discharging
	else if (statusStr.ICompare("Not charging") == 0)
		battery->state = BATTERY_NOT_CHARGING;
	else if (statusStr.ICompare("Empty") == 0)
		battery->state = BATTERY_DISCHARGING | BATTERY_CRITICAL_STATE;
	else
		battery->state = BATTERY_CRITICAL_STATE;

	return B_OK;
}


//	#pragma mark - Battery detection


status_t
SysFSDriverInterface::_DetectBatteries()
{
	BDirectory dir(kSysfsPowerSupply);
	BEntry entry;

	// Clean up any previous battery list
	for (int i = 0; i < fBatteries.CountItems(); i++)
		delete fBatteries.ItemAt(i);
	fBatteries.MakeEmpty();

	status_t overallStatus = B_ERROR;

	while (dir.GetNextEntry(&entry) == B_OK) {
		BPath path;
		if (entry.GetPath(&path) != B_OK)
			continue;

		// Check if this is a battery or AC adapter
		BString typeStr;
		if (_ReadStringAttr(path.Path(), "type", &typeStr) != B_OK)
			continue;

		sysfs_battery* battery = new(std::nothrow) sysfs_battery;
		if (battery == NULL)
			continue;

		battery->path = path.Path();
		battery->name = entry.Name();
		battery->is_battery = (typeStr.ICompare("Battery") == 0);
		battery->is_ac = (typeStr.ICompare("Mains") == 0);

		if (!battery->is_battery) {
			delete battery;
			continue;
		}

		battery->capacity = 0;
		battery->full_capacity = 0;
		battery->current_rate = 0;
		battery->voltage = 0;
		battery->state = BATTERY_CRITICAL_STATE;
		battery->init_status = B_OK;

		// Read the full capacity (prefer energy_full, fall back to charge_full)
		int32 fullVal = 0;
		if (_ReadIntAttr(path.Path(), "energy_full", &fullVal) == B_OK) {
			// energy_full is in microwatt-hours
			battery->full_capacity = fullVal / 1000;
		} else if (_ReadIntAttr(path.Path(), "charge_full", &fullVal) == B_OK) {
			// charge_full is in microamp-hours
			battery->full_capacity = fullVal / 1000;
		} else {
			battery->init_status = B_ERROR;
		}

		// Read design capacity for extended info
		int32 designVal = 0;
		if (_ReadIntAttr(path.Path(), "energy_full_design", &designVal) == B_OK)
			battery->full_capacity = max_c(battery->full_capacity,
				designVal / 1000);
		else if (_ReadIntAttr(path.Path(), "charge_full_design", &designVal)
				== B_OK)
			battery->full_capacity = max_c(battery->full_capacity,
				designVal / 1000);

		if (fBatteries.AddItem(battery))
			overallStatus = B_OK;
		else
			delete battery;
	}

	return overallStatus;
}


//	#pragma mark - Public interface


SysFSDriverInterface::SysFSDriverInterface()
	:
	fInterfaceLocker("sysfs power interface")
{
}


SysFSDriverInterface::~SysFSDriverInterface()
{
	for (int i = 0; i < fBatteries.CountItems(); i++)
		delete fBatteries.ItemAt(i);
}


status_t
SysFSDriverInterface::Connect()
{
	return _DetectBatteries();
}


status_t
SysFSDriverInterface::GetBatteryInfo(int32 index, battery_info* info)
{
	BAutolock autolock(fInterfaceLocker);
	if (index < 0 || index >= fBatteries.CountItems())
		return B_ERROR;

	sysfs_battery* battery = fBatteries.ItemAt(index);

	// Read current values
	int32 capacityVal = 0;
	if (_ReadIntAttr(battery->path.String(), "capacity", &capacityVal) == B_OK)
		battery->capacity = capacityVal;

	// Read current rate
	int32 rateVal = 0;
	if (_ReadIntAttr(battery->path.String(), "power_now", &rateVal) == B_OK)
		battery->current_rate = rateVal / 1000; // microwatts to milliwatts
	else if (_ReadIntAttr(battery->path.String(), "current_now", &rateVal)
			== B_OK)
		battery->current_rate = rateVal / 1000; // microamps to milliamps

	// Read voltage
	int32 voltageVal = 0;
	if (_ReadIntAttr(battery->path.String(), "voltage_now", &voltageVal)
			== B_OK)
		battery->voltage = voltageVal / 1000; // microwatts to milliwatts

	// Determine state from sysfs "status" file
	_ReadBatteryState(battery);

	// Populate the battery_info struct
	info->state = battery->state;
	info->capacity = battery->capacity;
	info->full_capacity = battery->full_capacity;
	info->current_rate = battery->current_rate;

	// Calculate time remaining
	if (battery->current_rate > 0
		&& (battery->state & BATTERY_DISCHARGING) != 0) {
		info->time_left = 3600 * battery->capacity / battery->current_rate;
	} else if (battery->current_rate > 0
		&& (battery->state & BATTERY_CHARGING) != 0) {
		info->time_left = 3600 * (battery->full_capacity - battery->capacity)
			/ battery->current_rate;
	} else {
		info->time_left = -1;
	}

	return B_OK;
}


status_t
SysFSDriverInterface::GetExtendedBatteryInfo(int32 index,
	acpi_extended_battery_info* info)
{
	BAutolock autolock(fInterfaceLocker);
	if (index < 0 || index >= fBatteries.CountItems())
		return B_ERROR;

	sysfs_battery* battery = fBatteries.ItemAt(index);
	memset(info, 0, sizeof(acpi_extended_battery_info));

	// Populate what we can from sysfs
	info->power_unit = ACPI_BATTERY_UNIT_MW;

	int32 val = 0;
	if (_ReadIntAttr(battery->path.String(), "energy_full_design", &val)
			== B_OK)
		info->design_capacity = val / 1000;
	else if (_ReadIntAttr(battery->path.String(), "charge_full_design", &val)
			== B_OK)
		info->design_capacity = val / 1000;

	if (_ReadIntAttr(battery->path.String(), "energy_full", &val) == B_OK)
		info->last_full_charge = val / 1000;
	else if (_ReadIntAttr(battery->path.String(), "charge_full", &val) == B_OK)
		info->last_full_charge = val / 1000;

	if (_ReadIntAttr(battery->path.String(), "voltage_min_design", &val)
			== B_OK)
		info->design_voltage = val / 1000;

	BString modelStr;
	if (_ReadStringAttr(battery->path.String(), "model_name", &modelStr)
			== B_OK) {
		strncpy(info->model_number, modelStr.String(),
			BATTERY_MAX_STRING_LENGTH - 1);
	}

	BString serialStr;
	if (_ReadStringAttr(battery->path.String(), "serial_number", &serialStr)
			== B_OK) {
		strncpy(info->serial_number, serialStr.String(),
			BATTERY_MAX_STRING_LENGTH - 1);
	}

	BString typeStr;
	if (_ReadStringAttr(battery->path.String(), "technology", &typeStr)
			== B_OK) {
		strncpy(info->type, typeStr.String(), BATTERY_MAX_STRING_LENGTH - 1);
	}

	BString manufacturerStr;
	if (_ReadStringAttr(battery->path.String(), "manufacturer",
			&manufacturerStr) == B_OK) {
		strncpy(info->oem_info, manufacturerStr.String(),
			BATTERY_MAX_STRING_LENGTH - 1);
	}

	return B_OK;
}


int32
SysFSDriverInterface::GetBatteryCount()
{
	return fBatteries.CountItems();
}


status_t
SysFSDriverInterface::_UpdateBatteryInfo()
{
	// Refresh detection in case batteries were hot-plugged (unlikely but
	// safe).  Then re-read cached values for each battery.
	_DetectBatteries();
	return B_OK;
}


void
SysFSDriverInterface::_WatchPowerStatus()
{
	const bigtime_t kUpdateInterval = 2000000;
		// every two seconds

	while (atomic_get(&fIsWatching) > 0) {
		_UpdateBatteryInfo();
		Broadcast(kMsgUpdate);
		acquire_sem_etc(fWaitSem, 1, B_RELATIVE_TIMEOUT, kUpdateInterval);
	}
}
