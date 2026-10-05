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

#include <dirent.h>

#include <Autolock.h>
#include <String.h>


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
SysFSDriverInterface::_ReadCapacityFamily(const char* path,
	sysfs_battery* battery)
{
	int32 energyNow = -1;
	int32 energyFull = -1;
	int32 chargeNow = -1;
	int32 chargeFull = -1;
	int32 percent = -1;
	int32 design = -1;

	_ReadIntAttr(path, "energy_now", &energyNow);
	_ReadIntAttr(path, "energy_full", &energyFull);
	_ReadIntAttr(path, "charge_now", &chargeNow);
	_ReadIntAttr(path, "charge_full", &chargeFull);
	_ReadIntAttr(path, "capacity", &percent);

	// Prefer a same-unit energy pair; some ECs report capacity=0 while
	// energy_* is still valid.
	if (energyNow >= 0 && energyFull > 0) {
		battery->capacity_kind = SYSFS_CAPACITY_ENERGY;
		battery->capacity = energyNow / 1000;
		battery->full_capacity = energyFull / 1000;
		return B_OK;
	}

	if (chargeNow >= 0 && chargeFull > 0) {
		battery->capacity_kind = SYSFS_CAPACITY_CHARGE;
		battery->capacity = chargeNow / 1000;
		battery->full_capacity = chargeFull / 1000;
		return B_OK;
	}

	// Percent-only batteries: keep the 0-100 scale the UI already expects.
	if (percent >= 0 && percent <= 100) {
		battery->capacity_kind = SYSFS_CAPACITY_PERCENT;
		battery->capacity = percent;
		battery->full_capacity = 100;
		return B_OK;
	}

	// Last resort: design capacity alone, still not mixed with percent.
	if (_ReadIntAttr(path, "energy_full_design", &design) == B_OK
			&& design > 0) {
		battery->capacity_kind = SYSFS_CAPACITY_ENERGY;
		battery->capacity = 0;
		battery->full_capacity = design / 1000;
		return B_OK;
	}
	if (_ReadIntAttr(path, "charge_full_design", &design) == B_OK
			&& design > 0) {
		battery->capacity_kind = SYSFS_CAPACITY_CHARGE;
		battery->capacity = 0;
		battery->full_capacity = design / 1000;
		return B_OK;
	}

	battery->capacity_kind = SYSFS_CAPACITY_NONE;
	battery->capacity = 0;
	battery->full_capacity = 0;
	return B_ERROR;
}


status_t
SysFSDriverInterface::_DetectBatteries()
{
	// POSIX readdir: BDirectory entry_ref iteration needs kernel node
	// refs that are unavailable when this runs on a host build.
	DIR* dir = opendir(fRoot.String());
	if (dir == NULL)
		return B_FILE_ERROR;

	// Clean up any previous battery list
	for (int i = 0; i < fBatteries.CountItems(); i++)
		delete fBatteries.ItemAt(i);
	fBatteries.MakeEmpty();

	status_t overallStatus = B_ERROR;
	struct dirent* ent;

	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;

		BString path(fRoot);
		path << "/" << ent->d_name;

		// Check if this is a battery or AC adapter
		BString typeStr;
		if (_ReadStringAttr(path.String(), "type", &typeStr) != B_OK)
			continue;

		sysfs_battery* battery = new(std::nothrow) sysfs_battery;
		if (battery == NULL)
			continue;

		battery->path = path;
		battery->name = ent->d_name;
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
		battery->capacity_kind = SYSFS_CAPACITY_NONE;
		battery->init_status = B_OK;

		_ReadCapacityFamily(path.String(), battery);

		if (fBatteries.AddItem(battery))
			overallStatus = B_OK;
		else
			delete battery;
	}

	closedir(dir);
	return overallStatus;
}


//	#pragma mark - Public interface


SysFSDriverInterface::SysFSDriverInterface(const char* powerSupplyRoot)
	:
	fInterfaceLocker("sysfs power interface")
{
	fRoot.SetTo(powerSupplyRoot != NULL ? powerSupplyRoot : kSysfsPowerSupply);
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

	// Re-read the capacity family; EC values change while discharging.
	_ReadCapacityFamily(battery->path.String(), battery);

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
		battery->voltage = voltageVal / 1000; // microvolts to millivolts

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

	// Keep design/last-full in the same unit family as capacity.
	int32 raw = 0;
	if (battery->capacity_kind == SYSFS_CAPACITY_CHARGE) {
		info->power_unit = ACPI_BATTERY_UNIT_MA;
		if (_ReadIntAttr(battery->path.String(), "charge_full_design",
				&raw) == B_OK && raw > 0)
			info->design_capacity = (uint32)raw / 1000;
		if (_ReadIntAttr(battery->path.String(), "charge_full", &raw)
				== B_OK && raw > 0)
			info->last_full_charge = (uint32)raw / 1000;
	} else if (battery->capacity_kind == SYSFS_CAPACITY_ENERGY) {
		info->power_unit = ACPI_BATTERY_UNIT_MW;
		if (_ReadIntAttr(battery->path.String(), "energy_full_design",
				&raw) == B_OK && raw > 0)
			info->design_capacity = (uint32)raw / 1000;
		if (_ReadIntAttr(battery->path.String(), "energy_full", &raw)
				== B_OK && raw > 0)
			info->last_full_charge = (uint32)raw / 1000;
	} else if (battery->capacity_kind == SYSFS_CAPACITY_PERCENT) {
		// Percent scale has no mWh/mAh counterpart; keep the UI scale.
		info->power_unit = ACPI_BATTERY_UNIT_MW;
		info->last_full_charge = 100;
	}

	int32 val = 0;
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
