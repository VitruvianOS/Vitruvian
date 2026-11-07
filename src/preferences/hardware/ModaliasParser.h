/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _MODALIAS_PARSER_H
#define _MODALIAS_PARSER_H


#include <stdint.h>

#include <string>
#include <vector>


// Parses kernel modalias strings (the MODALIAS uevent field and the
// sysfs modalias file): pci:, usb:, platform:, of:, sdio:, spi:, i2c:,
// input:, acpi:, virtio:, serio:, mmc: and friends. Unknown buses fall
// back to bus + raw remainder so nothing is ever silently dropped.


namespace Hardware {


struct ModaliasInfo {
	std::string			raw;
	std::string			bus;
	bool				hasVendor;
	bool				hasDevice;
	bool				hasSubVendor;
	bool				hasSubDevice;
	bool				hasClass;
	uint32_t				vendor;
	uint32_t				device;
	uint32_t				subVendor;
	uint32_t				subDevice;
	uint32_t				classCode;
	std::string			name;
	std::vector<std::string>	ofCompatibles;

						ModaliasInfo();

	bool				IsValid() const { return !bus.empty(); }
};


bool
ParseModalias(const std::string& text, ModaliasInfo& info);


}	// namespace Hardware


#endif	// _MODALIAS_PARSER_H
