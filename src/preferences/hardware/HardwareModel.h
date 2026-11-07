/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _HARDWARE_MODEL_H
#define _HARDWARE_MODEL_H


#include <stdint.h>

#include <string>
#include <vector>


// Data model shared by the Hardware preflet, its sysfs parsers and the
// host unit tests. Nothing in this header may depend on libbe so the
// parsers stay runnable on a plain Linux host.


namespace Hardware {


struct DeviceProperty {
	std::string	key;
	std::string	value;
};


// Which side of the tab split a device lands on.
enum DeviceTab {
	kTabPlugPlay = 0,
	kTabPlatform,
};


// Coarse grouping inside a tab; order doubles as the outline sort key.
enum DeviceCategory {
	kCategoryNone = 0,
	kCategoryPci,
	kCategoryUsb,
	kCategoryThunderbolt,
	kCategoryNvme,
	kCategoryDisks,
	kCategoryInput,
	kCategorySound,
	kCategoryDisplay,
	kCategoryNetwork,
	kCategoryBluetooth,
	kCategoryCamera,
	kCategoryPower,
	kCategoryAcpi,
	kCategoryPlatform,
	kCategoryI2c,
	kCategorySpi,
	kCategoryGpio,
	kCategoryPwm,
	kCategoryRegulator,
	kCategoryClock,
	kCategoryThermal,
	kCategorySensor,
	kCategoryMmc,
	kCategoryPanel,
	kCategoryTouchscreen,
	kCategoryModem,
	kCategoryRemoteProc,
};


// One discovered device. Field layout is udev-shaped on purpose: the
// live scanner fills it from libudev plus sysfs reads, the tests fill
// it from fixture trees, and both feed the same UI and report code.
struct DeviceRecord {
	std::string	syspath;
	std::string	kernelName;
	std::string	subsystem;
	std::string	devtype;
	std::string	driver;
	std::string	driverModule;
	std::string	devname;
	std::string	busAddress;
	std::string	vendorId;
	std::string	productId;
	std::string	subsystemVendorId;
	std::string	subsystemProductId;
	std::string	classId;
	std::string	revision;
	std::string	manufacturer;
	std::string	product;
	std::string	model;
	std::string	firmware;
	std::string	powerState;
	std::string	runtimePm;
	std::string	speed;
	std::string	dtCompatible;
	std::string	dtName;
	std::string	dtPath;
	std::string	acpiPath;
	std::string	acpiId;
	std::string	parentSyspath;
	std::string	modalias;
	std::string	usbPath;
	bool		hasOfNode;
	bool		hasDriver;

	std::vector<std::string>		resources;
	std::vector<DeviceProperty>	properties;
	std::vector<std::string>		parentChain;

									DeviceRecord();
};


// Summary header contents (DMI or DT model, CPU, memory, firmware,
// kernel). Read from /proc and sysfs; also fixture-driven in tests.
struct MachineInfo {
	std::string	model;
	std::string	vendor;
	std::string	cpuModel;
	std::string	cpuCores;
	std::string	cpuFrequency;
	std::string	memory;
	std::string	firmware;
	std::string	kernel;
	std::string	architecture;

								MachineInfo();
};


const char*
CategoryLabel(DeviceCategory category);


}	// namespace Hardware


#endif	// _HARDWARE_MODEL_H
