/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "HardwareReport.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "DeviceClassifier.h"


namespace Hardware {


static uint32_t
parse_id(const std::string& text)
{
	if (text.empty())
		return 0;

	return (uint32_t)strtoul(text.c_str(), NULL, 16);
}


static std::string
join(const std::vector<std::string>& values, const char* separator)
{
	std::string result;
	for (size_t i = 0; i < values.size(); i++) {
		if (i > 0)
			result += separator;
		result += values[i];
	}

	return result;
}


std::string
DeviceDisplayName(const DeviceRecord& record, const HwdataDatabase& names)
{
	if (!record.manufacturer.empty() && !record.product.empty())
		return record.manufacturer + " " + record.product;

	if (!record.manufacturer.empty() && !record.model.empty())
		return record.manufacturer + " " + record.model;

	if (!record.vendorId.empty() && !record.productId.empty()) {
		if (record.subsystem == "usb") {
			const char* vendor = names.UsbVendor(
				parse_id(record.vendorId));
			const char* product = names.UsbProduct(
				parse_id(record.vendorId),
				parse_id(record.productId));
			if (vendor != NULL && product != NULL)
				return std::string(vendor) + " " + product;
			if (product != NULL)
				return product;
		} else {
			const char* vendor = names.PciVendor(
				parse_id(record.vendorId));
			const char* product = names.PciProduct(
				parse_id(record.vendorId),
				parse_id(record.productId));
			if (vendor != NULL && product != NULL)
				return std::string(vendor) + " " + product;
			if (product != NULL)
				return product;
		}
	}

	if (!record.model.empty())
		return record.model;

	if (!record.product.empty())
		return record.product;

	return record.kernelName;
}


static std::string
describe_class(const DeviceRecord& record, const HwdataDatabase& names)
{
	if (record.classId.empty())
		return std::string();

	uint32_t classCode = parse_id(record.classId);
	char code[16];
	snprintf(code, sizeof(code), "0x%06x", classCode);

	std::string text(code);
	const char* className = NULL;
	if (record.subsystem == "usb")
		className = names.UsbClassCode(classCode);
	else
		className = names.PciClassCode(classCode);

	if (className != NULL) {
		text += " ";
		text += className;
	}

	return text;
}


std::string
BuildDeviceDetail(const DeviceRecord& record, const HwdataDatabase& names,
	bool includeProperties)
{
	std::string text;
	char line[512];

	snprintf(line, sizeof(line), "%s (%s)\n", record.kernelName.c_str(),
		record.subsystem.c_str());
	text += line;

	snprintf(line, sizeof(line), "    Name: %s\n",
		DeviceDisplayName(record, names).c_str());
	text += line;

	if (!record.vendorId.empty()) {
		snprintf(line, sizeof(line), "    Vendor: %s", 
			record.vendorId.c_str());
		text += line;
		const char* vendorName = record.subsystem == "usb"
			? names.UsbVendor(parse_id(record.vendorId))
			: names.PciVendor(parse_id(record.vendorId));
		if (vendorName != NULL) {
			snprintf(line, sizeof(line), " (%s)", vendorName);
			text += line;
		}
		text += "\n";
	}

	if (!record.productId.empty()) {
		snprintf(line, sizeof(line), "    Product: %s",
			record.productId.c_str());
		text += line;
		const char* productName = NULL;
		if (record.subsystem == "usb") {
			productName = names.UsbProduct(parse_id(record.vendorId),
				parse_id(record.productId));
		} else {
			productName = names.PciProduct(parse_id(record.vendorId),
				parse_id(record.productId));
		}
		if (productName != NULL) {
			snprintf(line, sizeof(line), " (%s)", productName);
			text += line;
		}
		text += "\n";
	}

	if (!record.subsystemVendorId.empty()
		|| !record.subsystemProductId.empty()) {
		const char* subsystemName = names.PciSubsystem(
			parse_id(record.vendorId), parse_id(record.subsystemVendorId),
			parse_id(record.productId),
			parse_id(record.subsystemProductId));
		snprintf(line, sizeof(line), "    Subsystem: %s:%s",
			record.subsystemVendorId.c_str(),
			record.subsystemProductId.c_str());
		text += line;
		if (subsystemName != NULL) {
			snprintf(line, sizeof(line), " (%s)", subsystemName);
			text += line;
		}
		text += "\n";
	}

	std::string classText = describe_class(record, names);
	if (!classText.empty()) {
		snprintf(line, sizeof(line), "    Class: %s\n", classText.c_str());
		text += line;
	}

	if (!record.busAddress.empty()) {
		snprintf(line, sizeof(line), "    Bus address: %s\n",
			record.busAddress.c_str());
		text += line;
	}

	if (!record.driver.empty()) {
		snprintf(line, sizeof(line), "    Driver: %s\n",
			record.driver.c_str());
		text += line;
	} else {
		text += "    Driver: (none bound)\n";
	}

	if (!record.modalias.empty()) {
		snprintf(line, sizeof(line), "    Modalias: %s\n",
			record.modalias.c_str());
		text += line;
	}

	if (!record.firmware.empty()) {
		snprintf(line, sizeof(line), "    Firmware: %s\n",
			record.firmware.c_str());
		text += line;
	}

	if (!record.runtimePm.empty()) {
		snprintf(line, sizeof(line), "    Runtime PM: %s\n",
			record.runtimePm.c_str());
		text += line;
	}

	if (!record.powerState.empty()) {
		snprintf(line, sizeof(line), "    Power control: %s\n",
			record.powerState.c_str());
		text += line;
	}

	if (!record.speed.empty()) {
		snprintf(line, sizeof(line), "    Speed: %s\n",
			record.speed.c_str());
		text += line;
	}

	if (!record.resources.empty()) {
		text += "    Resources:\n";
		for (size_t i = 0; i < record.resources.size(); i++) {
			snprintf(line, sizeof(line), "      %s\n",
				record.resources[i].c_str());
			text += line;
		}
	}

	if (!record.parentChain.empty()) {
		snprintf(line, sizeof(line), "    Parent: %s\n",
			join(record.parentChain, " > ").c_str());
		text += line;
	}

	if (record.hasOfNode || !record.dtCompatible.empty()) {
		snprintf(line, sizeof(line), "    Device tree compatible: %s\n",
			record.dtCompatible.c_str());
		text += line;
		snprintf(line, sizeof(line), "    Device tree node: %s\n",
			record.dtPath.c_str());
		text += line;
	}

	if (!record.acpiPath.empty()) {
		snprintf(line, sizeof(line), "    ACPI path: %s\n",
			record.acpiPath.c_str());
		text += line;
	}

	if (includeProperties && !record.properties.empty()) {
		text += "    Properties:\n";
		for (size_t i = 0; i < record.properties.size(); i++) {
			snprintf(line, sizeof(line), "      %s=%s\n",
				record.properties[i].key.c_str(),
				record.properties[i].value.c_str());
			text += line;
		}
	}

	return text;
}


std::string
BuildReport(const MachineInfo& machine,
	const std::vector<DeviceRecord>& records,
	const HwdataDatabase& names, const std::string& generatedAt)
{
	std::string text = "V\\OS hardware report\n";
	char line[512];

	snprintf(line, sizeof(line), "Generated: %s\n", generatedAt.c_str());
	text += line;
	snprintf(line, sizeof(line), "Kernel: %s (%s)\n", machine.kernel.c_str(),
		machine.architecture.c_str());
	text += line;

	text += "\nMachine\n";
	snprintf(line, sizeof(line), "  Model: %s\n", machine.model.c_str());
	text += line;
	snprintf(line, sizeof(line), "  Vendor: %s\n", machine.vendor.c_str());
	text += line;
	snprintf(line, sizeof(line), "  CPU: %s\n", machine.cpuModel.c_str());
	text += line;
	snprintf(line, sizeof(line), "  Cores: %s\n", machine.cpuCores.c_str());
	text += line;
	snprintf(line, sizeof(line), "  Frequency: %s\n",
		machine.cpuFrequency.c_str());
	text += line;
	snprintf(line, sizeof(line), "  Memory: %s\n", machine.memory.c_str());
	text += line;
	snprintf(line, sizeof(line), "  Firmware: %s\n",
		machine.firmware.c_str());
	text += line;

	// Group by category in enum order, matching the preflet outline.
	std::vector<DeviceCategory> order;
	for (int c = kCategoryPci; c <= kCategoryRemoteProc; c++)
		order.push_back((DeviceCategory)c);

	for (size_t i = 0; i < order.size(); i++) {
		bool headerWritten = false;
		for (size_t j = 0; j < records.size(); j++) {
			Classification classification
				= ClassifyRecord(records[j]);
			if (!classification.wanted
				|| classification.category != order[i])
				continue;

			if (!headerWritten) {
				snprintf(line, sizeof(line), "\n%s\n",
					CategoryLabel(order[i]));
				text += line;
				headerWritten = true;
			}

			text += "\n";
			text += BuildDeviceDetail(records[j], names, true);
		}
	}

	return text;
}


}	// namespace Hardware
