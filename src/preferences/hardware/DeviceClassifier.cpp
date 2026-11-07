/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "DeviceClassifier.h"

#include <stdlib.h>
#include <string.h>


namespace Hardware {


Classification::Classification()
	:
	tab(kTabPlugPlay),
	category(kCategoryNone),
	wanted(false)
{
}


static uint32_t
parse_class_code(const std::string& text)
{
	if (text.empty())
		return 0;

	return (uint32_t)strtoul(text.c_str(), NULL, 16);
}


static bool
starts_with(const std::string& text, const char* prefix)
{
	size_t length = strlen(prefix);
	return text.size() >= length && text.compare(0, length, prefix) == 0;
}


Classification
ClassifyRecord(const DeviceRecord& record)
{
	Classification result;
	const std::string& subsystem = record.subsystem;
	uint32_t classCode = parse_class_code(record.classId);

	if (subsystem == "pci") {
		result.tab = kTabPlugPlay;
		// NVMe shows up as a PCI storage class controller; give it
		// its own category so namespaces and controller sit together.
		if ((classCode & 0xffff00) == 0x010800)
			result.category = kCategoryNvme;
		// USB host controllers group with the devices behind them.
		else if ((classCode & 0xff0000) == 0x0c0000
			&& ((classCode >> 8) & 0xff) == 0x03)
			result.category = kCategoryUsb;
		else
			result.category = kCategoryPci;
		result.wanted = true;
		return result;
	}

	if (subsystem == "usb") {
		result.tab = kTabPlugPlay;
		result.category = kCategoryUsb;
		// Interfaces triple the list without telling the user more
		// than their driver already shows on the parent device.
		result.wanted = record.devtype != "usb_interface";
		return result;
	}

	if (subsystem == "thunderbolt" || subsystem == "usb4") {
		result.tab = kTabPlugPlay;
		result.category = kCategoryThunderbolt;
		result.wanted = true;
		return result;
	}

	if (subsystem == "nvme") {
		result.tab = kTabPlugPlay;
		result.category = kCategoryNvme;
		result.wanted = true;
		return result;
	}

	if (subsystem == "block") {
		result.tab = kTabPlugPlay;
		if (starts_with(record.kernelName, "nvme"))
			result.category = kCategoryNvme;
		else
			result.category = kCategoryDisks;
		result.wanted = true;
		return result;
	}

	if (subsystem == "scsi" && starts_with(record.kernelName, "sd")) {
		result.tab = kTabPlugPlay;
		result.category = kCategoryDisks;
		result.wanted = true;
		return result;
	}

	if (subsystem == "input" || subsystem == "hid" || subsystem == "serio") {
		result.tab = kTabPlugPlay;
		result.category = kCategoryInput;
		result.wanted = true;
		return result;
	}

	if (subsystem == "sound") {
		result.tab = kTabPlugPlay;
		result.category = kCategorySound;
		result.wanted = true;
		return result;
	}

	if (subsystem == "drm") {
		result.tab = kTabPlugPlay;
		result.category = kCategoryDisplay;
		// Only connectors carry status and EDID; the card and render
		// nodes are already represented by their PCI display device.
		result.wanted = record.kernelName.find('-')
			!= std::string::npos
			&& !starts_with(record.kernelName, "render");
		return result;
	}

	if (subsystem == "net") {
		result.tab = kTabPlugPlay;
		result.category = kCategoryNetwork;
		result.wanted = record.kernelName != "lo";
		return result;
	}

	if (subsystem == "bluetooth") {
		result.tab = kTabPlugPlay;
		result.category = kCategoryBluetooth;
		result.wanted = true;
		return result;
	}

	if (subsystem == "video4linux") {
		if (starts_with(record.kernelName, "video")) {
			result.tab = kTabPlugPlay;
			result.category = kCategoryCamera;
			result.wanted = true;
			return result;
		}

		if (starts_with(record.kernelName, "v4l-touch")) {
			result.tab = kTabPlatform;
			result.category = kCategoryTouchscreen;
			result.wanted = true;
			return result;
		}

		result.wanted = false;
		return result;
	}

	if (subsystem == "power_supply") {
		result.tab = kTabPlugPlay;
		result.category = kCategoryPower;
		result.wanted = true;
		return result;
	}

	if (subsystem == "acpi") {
		result.tab = kTabPlugPlay;
		result.category = kCategoryAcpi;
		// Plain ACPI nodes without a driver are firmware tables made
		// visible; the driver-bound ones are the useful set.
		result.wanted = record.hasDriver;
		return result;
	}

	if (subsystem == "platform") {
		result.tab = kTabPlatform;
		result.category = kCategoryPlatform;
		result.wanted = true;
		return result;
	}

	if (subsystem == "i2c") {
		result.tab = kTabPlatform;
		result.category = kCategoryI2c;
		result.wanted = !starts_with(record.kernelName, "i2c-");
		return result;
	}

	if (subsystem == "spi") {
		result.tab = kTabPlatform;
		result.category = kCategorySpi;
		result.wanted = true;
		return result;
	}

	if (subsystem == "gpio" || subsystem == "gpiochip") {
		result.tab = kTabPlatform;
		result.category = kCategoryGpio;
		result.wanted = true;
		return result;
	}

	if (subsystem == "pwm" || subsystem == "pwmchip") {
		result.tab = kTabPlatform;
		result.category = kCategoryPwm;
		result.wanted = true;
		return result;
	}

	if (subsystem == "regulator") {
		result.tab = kTabPlatform;
		result.category = kCategoryRegulator;
		result.wanted = true;
		return result;
	}

	if (subsystem == "clk") {
		result.tab = kTabPlatform;
		result.category = kCategoryClock;
		result.wanted = true;
		return result;
	}

	if (subsystem == "thermal") {
		result.tab = kTabPlatform;
		result.category = kCategoryThermal;
		result.wanted = starts_with(record.kernelName, "thermal_zone");
		return result;
	}

	if (subsystem == "hwmon" || subsystem == "iio") {
		result.tab = kTabPlatform;
		result.category = kCategorySensor;
		result.wanted = true;
		return result;
	}

	if (subsystem == "mmc" || subsystem == "mmc_host"
		|| subsystem == "sdio") {
		result.tab = kTabPlatform;
		result.category = kCategoryMmc;
		result.wanted = true;
		return result;
	}

	if (subsystem == "backlight") {
		result.tab = kTabPlatform;
		result.category = kCategoryPanel;
		result.wanted = true;
		return result;
	}

	if (subsystem == "touchscreen") {
		result.tab = kTabPlatform;
		result.category = kCategoryTouchscreen;
		result.wanted = true;
		return result;
	}

	if (subsystem == "wwan" || starts_with(record.kernelName, "wwan")
		|| starts_with(record.kernelName, "cdc-wdm")) {
		result.tab = kTabPlatform;
		result.category = kCategoryModem;
		result.wanted = true;
		return result;
	}

	if (subsystem == "remoteproc" || subsystem == "rpmsg") {
		result.tab = kTabPlatform;
		result.category = kCategoryRemoteProc;
		result.wanted = true;
		return result;
	}

	result.wanted = false;
	return result;
}


}	// namespace Hardware
