/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef SYSFS_DRIVER_INTERFACE_H
#define SYSFS_DRIVER_INTERFACE_H


#include "DriverInterface.h"

#include <Locker.h>
#include <ObjectList.h>
#include <String.h>


// Battery backed by a /sys/class/power_supply entry.
struct sysfs_battery {
	BString		path;
	BString		name;
	bool		is_battery;
	bool		is_ac;
	int8		state;
	int32		capacity;
	int32		full_capacity;
	int32		current_rate;
	int32		voltage;
	status_t	init_status;
};


class SysFSDriverInterface : public PowerStatusDriverInterface {
public:
								SysFSDriverInterface();
	virtual						~SysFSDriverInterface();

	virtual status_t			Connect();
	virtual status_t 			GetBatteryInfo(int32 index, battery_info* info);
	virtual status_t	 		GetExtendedBatteryInfo(int32 index,
									acpi_extended_battery_info* info);

	virtual int32				GetBatteryCount();

protected:
	virtual status_t 			_UpdateBatteryInfo();
	virtual void				_WatchPowerStatus();

private:
	status_t					_DetectBatteries();
	status_t					_ReadBatteryState(sysfs_battery* battery);

	static status_t				_ReadIntAttr(const char* path,
									const char* attr, int32* value);
	static status_t				_ReadStringAttr(const char* path,
									const char* attr, BString* value);

	BLocker						fInterfaceLocker;
	BObjectList<sysfs_battery>	fBatteries;
};

#endif	// SYSFS_DRIVER_INTERFACE_H
