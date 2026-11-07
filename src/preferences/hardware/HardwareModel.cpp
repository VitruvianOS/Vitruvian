/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "HardwareModel.h"


namespace Hardware {


DeviceRecord::DeviceRecord()
	:
	hasOfNode(false),
	hasDriver(false)
{
}


MachineInfo::MachineInfo()
{
}


const char*
CategoryLabel(DeviceCategory category)
{
	switch (category) {
		case kCategoryPci:
			return "PCI / PCIe";
		case kCategoryUsb:
			return "USB";
		case kCategoryThunderbolt:
			return "Thunderbolt";
		case kCategoryNvme:
			return "NVMe";
		case kCategoryDisks:
			return "Disks";
		case kCategoryInput:
			return "Input";
		case kCategorySound:
			return "Sound";
		case kCategoryDisplay:
			return "Display";
		case kCategoryNetwork:
			return "Network";
		case kCategoryBluetooth:
			return "Bluetooth";
		case kCategoryCamera:
			return "Cameras";
		case kCategoryPower:
			return "Power";
		case kCategoryAcpi:
			return "ACPI";
		case kCategoryPlatform:
			return "Platform";
		case kCategoryI2c:
			return "I2C";
		case kCategorySpi:
			return "SPI";
		case kCategoryGpio:
			return "GPIO";
		case kCategoryPwm:
			return "PWM";
		case kCategoryRegulator:
			return "Regulators";
		case kCategoryClock:
			return "Clocks";
		case kCategoryThermal:
			return "Thermal";
		case kCategorySensor:
			return "Sensors";
		case kCategoryMmc:
			return "MMC / SDIO";
		case kCategoryPanel:
			return "Panels / Backlight";
		case kCategoryTouchscreen:
			return "Touchscreens";
		case kCategoryModem:
			return "Modems";
		case kCategoryRemoteProc:
			return "Remote processors";
		default:
			return "Other";
	}
}


}	// namespace Hardware
