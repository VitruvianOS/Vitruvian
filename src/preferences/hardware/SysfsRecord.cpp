/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "SysfsRecord.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>
#include <sstream>

#include "EdidParser.h"
#include "ModaliasParser.h"


namespace Hardware {


static const uint32_t kIoResourceIo = 0x00000100;
static const uint32_t kIoResourceMem = 0x00000200;
static const uint32_t kIoResourceIrq = 0x00000400;
static const uint32_t kIoResourcePrefetch = 0x00002000;


std::string
ReadTrimmedFile(const std::string& path)
{
	std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
	if (!file.is_open())
		return std::string();

	std::ostringstream buffer;
	buffer << file.rdbuf();
	std::string text = buffer.str();

	// Trailing NULs matter: device tree attribute files end with one.
	while (!text.empty() && (text[text.size() - 1] == '\n'
		|| text[text.size() - 1] == '\r' || text[text.size() - 1] == ' '
		|| text[text.size() - 1] == '\t' || text[text.size() - 1] == '\0'))
		text.erase(text.size() - 1);

	return text;
}


static std::string
base_name(const std::string& path)
{
	size_t slash = path.rfind('/');
	if (slash == std::string::npos)
		return path;

	return path.substr(slash + 1);
}


static std::string
dir_name(const std::string& path)
{
	size_t slash = path.rfind('/');
	if (slash == std::string::npos)
		return std::string();

	if (slash == 0)
		return std::string("/");

	return path.substr(0, slash);
}


static std::string
read_link_basename(const std::string& path)
{
	char buffer[1024];
	ssize_t length = readlink(path.c_str(), buffer, sizeof(buffer) - 1);
	if (length < 0)
		return std::string();

	buffer[length] = '\0';
	std::string target(buffer);
	return base_name(target);
}


static std::string
strip_hex_prefix(const std::string& text)
{
	if (text.size() > 2 && text[0] == '0' && (text[1] == 'x'
		|| text[1] == 'X'))
		return text.substr(2);

	return text;
}


static void
add_property(DeviceRecord& record, const std::string& key,
	const std::string& value)
{
	DeviceProperty property;
	property.key = key;
	property.value = value;
	record.properties.push_back(property);
}


void
ApplyUeventFile(const std::string& path, DeviceRecord& record)
{
	std::ifstream file(path.c_str());
	if (!file.is_open())
		return;

	std::string line;
	while (std::getline(file, line)) {
		size_t equals = line.find('=');
		if (equals == std::string::npos || equals == 0)
			continue;

		std::string key = line.substr(0, equals);
		std::string value = line.substr(equals + 1);
		add_property(record, key, value);

		if (key == "DRIVER") {
			if (record.driver.empty()) {
				record.driver = value;
				record.hasDriver = !value.empty();
			}
		} else if (key == "MODALIAS") {
			if (record.modalias.empty())
				record.modalias = value;
		} else if (key == "DEVNAME") {
			record.devname = value;
		} else if (key == "FIRMWARE") {
			if (record.firmware.empty())
				record.firmware = value;
		} else if (key == "ACPI_DEVICE_PATH") {
			if (record.acpiPath.empty())
				record.acpiPath = value;
		} else if (key == "OF_NAME") {
			if (record.dtName.empty())
				record.dtName = value;
		} else if (key == "OF_COMPATIBLE_0") {
			if (record.dtCompatible.empty())
				record.dtCompatible = value;
		} else if (key == "ID_USB_INTERFACES") {
			// udev-only; kept in properties.
		}
	}
}


void
ParsePciResourceFile(const std::string& path,
	std::vector<std::string>& resources)
{
	std::ifstream file(path.c_str());
	if (!file.is_open())
		return;

	char line[256];
	while (file.getline(line, sizeof(line))) {
		unsigned long long start = 0;
		unsigned long long end = 0;
		unsigned long long flags = 0;
		if (sscanf(line, "%llx %llx %llx", &start, &end, &flags) != 3)
			continue;

		char text[128];
		if (flags & kIoResourceIrq) {
			snprintf(text, sizeof(text), "IRQ %llu", start);
			resources.push_back(text);
		} else if (flags & kIoResourceMem) {
			if (flags & kIoResourcePrefetch)
				snprintf(text, sizeof(text),
					"Prefetchable memory 0x%llx-0x%llx", start,
					end);
			else
				snprintf(text, sizeof(text),
					"Memory 0x%llx-0x%llx", start, end);
			resources.push_back(text);
		} else if (flags & kIoResourceIo) {
			snprintf(text, sizeof(text), "I/O 0x%llx-0x%llx", start,
				end);
			resources.push_back(text);
		}
	}
}


static void
_read_pci_ids(const std::string& dir, DeviceRecord& record)
{
	std::string vendor = ReadTrimmedFile(dir + "/vendor");
	std::string device = ReadTrimmedFile(dir + "/device");
	if (vendor.empty() && device.empty())
		return;

	record.vendorId = strip_hex_prefix(vendor);
	record.productId = strip_hex_prefix(device);
	record.subsystemVendorId
		= strip_hex_prefix(ReadTrimmedFile(dir + "/subsystem_vendor"));
	record.subsystemProductId
		= strip_hex_prefix(ReadTrimmedFile(dir + "/subsystem_device"));
	record.classId = strip_hex_prefix(ReadTrimmedFile(dir + "/class"));
	record.revision = strip_hex_prefix(ReadTrimmedFile(dir + "/revision"));

	if (record.busAddress.empty())
		record.busAddress = record.kernelName;

	ParsePciResourceFile(dir + "/resource", record.resources);

	// The resource file only lists ranges; the IRQ lives in its own
	// file.
	std::string irq = ReadTrimmedFile(dir + "/irq");
	if (!irq.empty()) {
		char text[64];
		snprintf(text, sizeof(text), "IRQ %s", irq.c_str());
		record.resources.push_back(text);
	}
}


static void
_read_usb_device(const std::string& dir, DeviceRecord& record)
{
	std::string vendor = ReadTrimmedFile(dir + "/idVendor");
	if (vendor.empty())
		return;

	record.vendorId = vendor;
	record.productId = ReadTrimmedFile(dir + "/idProduct");
	record.revision = ReadTrimmedFile(dir + "/bcdDevice");
	record.classId = ReadTrimmedFile(dir + "/bDeviceClass");

	std::string manufacturer = ReadTrimmedFile(dir + "/manufacturer");
	if (!manufacturer.empty())
		record.manufacturer = manufacturer;

	std::string product = ReadTrimmedFile(dir + "/product");
	if (!product.empty())
		record.product = product;

	std::string speed = ReadTrimmedFile(dir + "/speed");
	if (!speed.empty())
		record.speed = speed + " Mbps";

	if (record.busAddress.empty())
		record.busAddress = record.kernelName;
}


static void
_read_drm_connector(const std::string& dir, DeviceRecord& record)
{
	std::string status = ReadTrimmedFile(dir + "/status");
	if (!status.empty())
		add_property(record, "DRM_STATUS", status);

	std::string enabled = ReadTrimmedFile(dir + "/enabled");
	if (!enabled.empty())
		add_property(record, "DRM_ENABLED", enabled);

	std::string dpms = ReadTrimmedFile(dir + "/power/dpms");
	if (dpms.empty())
		dpms = ReadTrimmedFile(dir + "/dpms");
	if (!dpms.empty())
		add_property(record, "DRM_DPMS", dpms);

	std::ifstream modes(dir + "/modes");
	if (modes.is_open()) {
		std::string line;
		while (std::getline(modes, line)) {
			if (!line.empty())
				add_property(record, "DRM_MODE", line);
		}
	}

	std::ifstream edidFile(dir + "/edid", std::ios::in | std::ios::binary);
	if (edidFile.is_open()) {
		uint8_t data[512];
		edidFile.read((char*)data, sizeof(data));
		std::streamsize length = edidFile.gcount();
		if (length >= 128) {
			EdidInfo info;
			if (ParseEdid(data, (size_t)length, info)) {
				if (info.hasName)
					record.model = info.name;
				if (info.hasPreferredMode)
					add_property(record, "EDID_MODE",
						info.preferredMode);
				char ids[64];
				snprintf(ids, sizeof(ids), "%04x:%04x",
					info.manufacturer, info.product);
				add_property(record, "EDID_IDS", ids);
			}
		}
	}
}


static void
_read_input_device(const std::string& dir, DeviceRecord& record)
{
	std::string name = ReadTrimmedFile(dir + "/name");
	if (!name.empty())
		record.model = name;

	std::string phys = ReadTrimmedFile(dir + "/phys");
	if (!phys.empty())
		add_property(record, "INPUT_PHYS", phys);

	std::string bus = ReadTrimmedFile(dir + "/id/bustype");
	std::string vendor = ReadTrimmedFile(dir + "/id/vendor");
	std::string product = ReadTrimmedFile(dir + "/id/product");
	if (!vendor.empty())
		record.vendorId = strip_hex_prefix(vendor);
	if (!product.empty())
		record.productId = strip_hex_prefix(product);
	if (!bus.empty())
		add_property(record, "INPUT_BUS", strip_hex_prefix(bus));

	std::string uniq = ReadTrimmedFile(dir + "/uniq");
	if (!uniq.empty())
		add_property(record, "INPUT_UNIQ", uniq);
}


static void
_read_hwmon(const std::string& dir, DeviceRecord& record)
{
	std::string name = ReadTrimmedFile(dir + "/name");
	if (!name.empty())
		record.model = name;

	DIR* listing = opendir(dir.c_str());
	if (listing == NULL)
		return;

	struct dirent* entry;
	while ((entry = readdir(listing)) != NULL) {
		std::string fileName(entry->d_name);
		size_t label = fileName.find("_label");
		if (label == std::string::npos || label == 0)
			continue;

		std::string labelValue = ReadTrimmedFile(dir + "/" + fileName);
		std::string inputName = fileName.substr(0, label);
		std::string inputValue = ReadTrimmedFile(dir + "/" + inputName
			+ "_input");
		if (inputValue.empty())
			continue;

		// millidegrees or millivolts as reported; keep raw with the
		// label so the UI can pick a unit.
		add_property(record, "HWMON_" + labelValue, inputValue);
	}
	closedir(listing);
}


static void
_read_thermal_zone(const std::string& dir, DeviceRecord& record)
{
	std::string type = ReadTrimmedFile(dir + "/type");
	if (!type.empty())
		record.model = type;

	std::string temp = ReadTrimmedFile(dir + "/temp");
	if (!temp.empty())
		add_property(record, "TEMP_MILLC", temp);
}


static void
_read_backlight(const std::string& dir, DeviceRecord& record)
{
	std::string type = ReadTrimmedFile(dir + "/type");
	if (!type.empty())
		add_property(record, "BACKLIGHT_TYPE", type);

	std::string brightness = ReadTrimmedFile(dir + "/brightness");
	if (!brightness.empty())
		add_property(record, "BACKLIGHT_BRIGHTNESS", brightness);

	std::string maximum = ReadTrimmedFile(dir + "/max_brightness");
	if (!maximum.empty())
		add_property(record, "BACKLIGHT_MAX", maximum);
}


static void
_read_regulator(const std::string& dir, DeviceRecord& record)
{
	std::string name = ReadTrimmedFile(dir + "/name");
	if (!name.empty())
		record.model = name;

	std::string microvolts = ReadTrimmedFile(dir + "/microvolts");
	if (!microvolts.empty())
		add_property(record, "REGULATOR_MICROVOLTS", microvolts);

	std::string state = ReadTrimmedFile(dir + "/state");
	if (!state.empty())
		add_property(record, "REGULATOR_STATE", state);
}


static void
_read_gpio_chip(const std::string& dir, DeviceRecord& record)
{
	std::string label = ReadTrimmedFile(dir + "/label");
	if (!label.empty())
		record.model = label;

	std::string ngpio = ReadTrimmedFile(dir + "/ngpio");
	if (!ngpio.empty())
		add_property(record, "GPIO_COUNT", ngpio);
}


static void
_read_remote_proc(const std::string& dir, DeviceRecord& record)
{
	std::string name = ReadTrimmedFile(dir + "/name");
	if (!name.empty())
		record.model = name;

	std::string state = ReadTrimmedFile(dir + "/state");
	if (!state.empty())
		add_property(record, "REMOTEPROC_STATE", state);
}


static void
_read_of_node(const std::string& dir, DeviceRecord& record)
{
	std::string compatible = ReadTrimmedFile(dir + "/of_node/compatible");
	if (!compatible.empty()) {
		// The sysfs file separates the list with NUL bytes.
		std::string first = compatible;
		size_t nul = compatible.find('\0');
		if (nul != std::string::npos)
			first = compatible.substr(0, nul);

		record.hasOfNode = true;
		if (record.dtCompatible.empty())
			record.dtCompatible = first;

		std::string all;
		for (size_t i = 0; i < compatible.size(); i++) {
			char c = compatible[i];
			if (c == '\0')
				all += ",";
			else
				all += c;
		}
		if (!all.empty() && all[all.size() - 1] == ',')
			all.erase(all.size() - 1);
		add_property(record, "OF_COMPATIBLES", all);
	}

	std::string name = ReadTrimmedFile(dir + "/of_node/name");
	if (!name.empty()) {
		record.hasOfNode = true;
		if (record.dtName.empty())
			record.dtName = name;
	}

	std::string model = ReadTrimmedFile(dir + "/of_node/model");
	if (!model.empty() && record.model.empty())
		record.model = model;

	std::string status = ReadTrimmedFile(dir + "/of_node/status");
	if (!status.empty())
		add_property(record, "OF_STATUS", status);

	char buffer[1024];
	ssize_t length = readlink((dir + "/of_node").c_str(), buffer,
		sizeof(buffer) - 1);
	if (length > 0) {
		buffer[length] = '\0';
		std::string target(buffer);
		static const char* kBase = "/firmware/devicetree/base/";
		size_t base = target.find(kBase);
		if (base != std::string::npos)
			record.dtPath = target.substr(base + strlen(kBase));
	}
}


static void
_read_power(const std::string& dir, DeviceRecord& record)
{
	std::string runtimeStatus = ReadTrimmedFile(dir + "/power/runtime_status");
	if (!runtimeStatus.empty())
		record.runtimePm = runtimeStatus;

	std::string control = ReadTrimmedFile(dir + "/power/control");
	if (!control.empty())
		record.powerState = control;
}


static void
_read_block(const std::string& dir, DeviceRecord& record)
{
	std::string size = ReadTrimmedFile(dir + "/size");
	if (!size.empty())
		add_property(record, "SECTORS", size);

	std::string removable = ReadTrimmedFile(dir + "/removable");
	if (!removable.empty())
		add_property(record, "REMOVABLE", removable);

	std::string readOnly = ReadTrimmedFile(dir + "/ro");
	if (!readOnly.empty())
		add_property(record, "READ_ONLY", readOnly);

	std::string deviceDir = dir + "/device";
	std::string vendor = ReadTrimmedFile(deviceDir + "/vendor");
	std::string model = ReadTrimmedFile(deviceDir + "/model");
	if (!vendor.empty())
		record.manufacturer = vendor;
	if (!model.empty())
		record.model = model;

	std::string rotational = ReadTrimmedFile(dir + "/queue/rotational");
	if (!rotational.empty())
		add_property(record, "ROTATIONAL", rotational);
}


static void
_read_net(const std::string& dir, DeviceRecord& record)
{
	std::string address = ReadTrimmedFile(dir + "/address");
	if (!address.empty())
		add_property(record, "MAC_ADDRESS", address);

	std::string operstate = ReadTrimmedFile(dir + "/operstate");
	if (!operstate.empty())
		add_property(record, "OPERSTATE", operstate);

	std::string speed = ReadTrimmedFile(dir + "/speed");
	if (!speed.empty())
		record.speed = speed + " Mbps";

	std::string duplex = ReadTrimmedFile(dir + "/duplex");
	if (!duplex.empty())
		add_property(record, "DUPLEX", duplex);

	std::string mtu = ReadTrimmedFile(dir + "/mtu");
	if (!mtu.empty())
		add_property(record, "MTU", mtu);
}


static void
_read_acpi(const std::string& dir, DeviceRecord& record)
{
	size_t colon = record.kernelName.find(':');
	if (colon != std::string::npos)
		record.acpiId = record.kernelName.substr(0, colon);
	else
		record.acpiId = record.kernelName;

	std::string path = ReadTrimmedFile(dir + "/path");
	if (!path.empty())
		record.acpiPath = path;

	std::string description = ReadTrimmedFile(dir + "/description");
	if (!description.empty())
		add_property(record, "ACPI_DESCRIPTION", description);

	std::string statusBar = ReadTrimmedFile(dir + "/status");
	if (!statusBar.empty())
		add_property(record, "ACPI_STATUS", statusBar);
}


bool
ReadDeviceDir(const std::string& dir, DeviceRecord& record)
{
	struct stat st;
	if (stat(dir.c_str(), &st) < 0 || !S_ISDIR(st.st_mode))
		return false;

	record.syspath = dir;
	record.kernelName = base_name(dir);
	record.subsystem = read_link_basename(dir + "/subsystem");
	record.driver = read_link_basename(dir + "/driver");
	record.hasDriver = !record.driver.empty();

	std::string modalias = ReadTrimmedFile(dir + "/modalias");
	if (!modalias.empty())
		record.modalias = modalias;

	ApplyUeventFile(dir + "/uevent", record);

	if (!record.modalias.empty()) {
		ModaliasInfo info;
		if (ParseModalias(record.modalias, info)) {
			if (info.bus == "of" && !info.ofCompatibles.empty()
				&& record.dtCompatible.empty())
				record.dtCompatible = info.ofCompatibles[0];

			if (info.bus == "acpi" && record.acpiId.empty())
				record.acpiId = info.name;
		}
	}

	_read_pci_ids(dir, record);
	_read_usb_device(dir, record);
	_read_drm_connector(dir, record);
	_read_input_device(dir, record);
	_read_hwmon(dir, record);
	_read_thermal_zone(dir, record);
	_read_backlight(dir, record);
	_read_regulator(dir, record);
	_read_gpio_chip(dir, record);
	_read_remote_proc(dir, record);
	_read_of_node(dir, record);
	_read_power(dir, record);
	_read_block(dir, record);
	_read_net(dir, record);
	_read_acpi(dir, record);

	return true;
}


std::vector<std::string>
ParentChain(const std::string& dir)
{
	std::vector<std::string> chain;
	std::string current = dir;

	for (int i = 0; i < 64; i++) {
		std::string parent = dir_name(current);
		if (parent.empty() || parent == current)
			break;

		std::string name = base_name(parent);
		if (name == "devices" || name == "bus" || name == "class"
			|| parent == "/sys" || parent == "/")
			break;

		chain.push_back(parent);
		current = parent;
	}

	return chain;
}


}	// namespace Hardware
