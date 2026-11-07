/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "HwdataDatabase.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fstream>


namespace Hardware {


static const char* kPciIdsPaths[] = {
	"/usr/share/misc/pci.ids",
	"/usr/share/hwdata/pci.ids",
	"/usr/share/pci.ids",
	NULL
};


static const char* kUsbIdsPaths[] = {
	"/usr/share/misc/usb.ids",
	"/usr/share/hwdata/usb.ids",
	"/usr/share/usb.ids",
	NULL
};


static std::string
trim_name(const std::string& line, size_t offset)
{
	size_t start = offset;
	while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
		start++;

	size_t end = line.size();
	while (end > start && (line[end - 1] == ' ' || line[end - 1] == '\t'
		|| line[end - 1] == '\r'))
		end--;

	if (start >= end)
		return std::string();

	return line.substr(start, end - start);
}


static bool
parse_hex(const std::string& text, size_t offset, size_t length,
	uint32_t& value)
{
	if (offset + length > text.size() || length == 0)
		return false;

	uint32_t result = 0;
	for (size_t i = 0; i < length; i++) {
		char c = text[offset + i];
		uint32_t digit;
		if (c >= '0' && c <= '9')
			digit = c - '0';
		else if (c >= 'a' && c <= 'f')
			digit = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')
			digit = c - 'A' + 10;
		else
			return false;

		result = (result << 4) | digit;
	}

	value = result;
	return true;
}


static size_t
count_leading_tabs(const std::string& line)
{
	size_t i = 0;
	while (i < line.size() && line[i] == '\t')
		i++;
	return i;
}


HwdataDatabase::HwdataDatabase()
{
}


void
HwdataDatabase::_ClearPci()
{
	fPciVendors.clear();
	fPciProducts.clear();
	fPciSubsystems.clear();
	fPciClasses.clear();
}


void
HwdataDatabase::_ClearUsb()
{
	fUsbVendors.clear();
	fUsbProducts.clear();
	fUsbClasses.clear();
}


bool
HwdataDatabase::LoadPci(const char* path)
{
	_ClearPci();
	if (path == NULL)
		return false;

	return _LoadPciFile(path);
}


bool
HwdataDatabase::LoadUsb(const char* path)
{
	_ClearUsb();
	if (path == NULL)
		return false;

	return _LoadUsbFile(path);
}


bool
HwdataDatabase::_LoadPciFile(const std::string& path)
{
	std::ifstream file(path.c_str());
	if (!file.is_open())
		return false;

	uint32_t currentVendor = 0;
	uint32_t currentProduct = 0;
	uint32_t currentClassBase = 0;
	bool haveVendor = false;
	bool haveProduct = false;
	bool inClassSection = false;

	std::string line;
	while (std::getline(file, line)) {
		if (line.empty() || line[0] == '#')
			continue;

		size_t tabs = count_leading_tabs(line);
		size_t idStart = tabs;
		if (idStart >= line.size())
			continue;

		if (tabs == 0 && line.size() > idStart + 1 && line[idStart] == 'C'
			&& line[idStart + 1] == ' ') {
			// Class line: "C 03  Display controller".
			uint32_t classBase = 0;
			if (parse_hex(line, idStart + 2, 2, classBase)) {
				std::string name = trim_name(line, idStart + 4);
				if (!name.empty())
					fPciClasses[classBase] = name;
				currentClassBase = classBase;
				inClassSection = true;
				haveVendor = false;
				haveProduct = false;
			}
			continue;
		}

		size_t idEnd = idStart;
		while (idEnd < line.size() && isxdigit((unsigned char)line[idEnd]))
			idEnd++;

		size_t idLength = idEnd - idStart;
		uint32_t id = 0;
		if (!parse_hex(line, idStart, idLength, id))
			continue;

		if (tabs == 1 && inClassSection) {
			// Subclass line under the current class: two digits,
			// e.g. "00  VGA compatible controller".
			if (idLength != 2)
				continue;

			std::string name = trim_name(line, idEnd);
			if (name.empty())
				continue;

			fPciClasses[(currentClassBase << 8) | id] = name;
		} else if (tabs == 0) {
			// Vendor line: exactly four hex digits.
			if (idLength != 4)
				continue;

			std::string name = trim_name(line, idEnd);
			if (name.empty())
				continue;

			fPciVendors[id] = name;
			currentVendor = id;
			haveVendor = true;
			haveProduct = false;
			inClassSection = false;
		} else if (tabs == 1 && haveVendor) {
			// Product line under the current vendor.
			if (idLength != 4)
				continue;

			std::string name = trim_name(line, idEnd);
			if (name.empty())
				continue;

			fPciProducts[((uint64_t)currentVendor << 16) | id] = name;
			currentProduct = id;
			haveProduct = true;
		} else if (tabs == 2 && haveVendor && haveProduct) {
			// Subsystem line: eight hex digits, subsystem vendor in
			// the high half, subsystem device in the low half.
			if (idLength != 8)
				continue;

			std::string name = trim_name(line, idEnd);
			if (name.empty())
				continue;

			uint64_t key = ((uint64_t)currentVendor << 48)
				| ((uint64_t)currentProduct << 32)
				| ((uint64_t)id & 0xffffffffULL);
			fPciSubsystems[key] = name;
		}
	}

	return !fPciVendors.empty();
}


bool
HwdataDatabase::_LoadUsbFile(const std::string& path)
{
	std::ifstream file(path.c_str());
	if (!file.is_open())
		return false;

	uint32_t currentVendor = 0;
	bool haveVendor = false;

	std::string line;
	while (std::getline(file, line)) {
		if (line.empty() || line[0] == '#')
			continue;

		size_t tabs = count_leading_tabs(line);
		size_t idStart = tabs;
		if (idStart >= line.size())
			continue;

		if (tabs == 0 && line.size() > idStart + 1 && line[idStart] == 'C'
			&& line[idStart + 1] == ' ') {
			// Class line: "C 03  Communications Device".
			uint32_t classCode = 0;
			if (parse_hex(line, idStart + 2, 2, classCode)) {
				std::string name = trim_name(line, idStart + 4);
				if (!name.empty())
					fUsbClasses[classCode] = name;
			}
			continue;
		}

		size_t idEnd = idStart;
		while (idEnd < line.size() && isxdigit((unsigned char)line[idEnd]))
			idEnd++;

		size_t idLength = idEnd - idStart;
		uint32_t id = 0;
		if (!parse_hex(line, idStart, idLength, id))
			continue;

		if (tabs == 0) {
			if (idLength != 4)
				continue;

			std::string name = trim_name(line, idEnd);
			if (name.empty())
				continue;

			fUsbVendors[id] = name;
			currentVendor = id;
			haveVendor = true;
		} else if (tabs == 1 && haveVendor) {
			if (idLength != 4)
				continue;

			std::string name = trim_name(line, idEnd);
			if (name.empty())
				continue;

			fUsbProducts[((uint64_t)currentVendor << 16) | id] = name;
		}
	}

	return !fUsbVendors.empty();
}


std::string
HwdataDatabase::FindPciIds()
{
	for (int i = 0; kPciIdsPaths[i] != NULL; i++) {
		std::ifstream file(kPciIdsPaths[i]);
		if (file.is_open())
			return std::string(kPciIdsPaths[i]);
	}

	return std::string();
}


std::string
HwdataDatabase::FindUsbIds()
{
	for (int i = 0; kUsbIdsPaths[i] != NULL; i++) {
		std::ifstream file(kUsbIdsPaths[i]);
		if (file.is_open())
			return std::string(kUsbIdsPaths[i]);
	}

	return std::string();
}


const char*
HwdataDatabase::PciVendor(uint32_t vendor) const
{
	IdNameMap::const_iterator it = fPciVendors.find(vendor);
	if (it == fPciVendors.end())
		return NULL;

	return it->second.c_str();
}


const char*
HwdataDatabase::PciProduct(uint32_t vendor, uint32_t product) const
{
	KeyedNameMap::const_iterator it = fPciProducts.find(
		((uint64_t)vendor << 16) | product);
	if (it == fPciProducts.end())
		return NULL;

	return it->second.c_str();
}


const char*
HwdataDatabase::PciSubsystem(uint32_t vendor, uint32_t subsystemVendor,
	uint32_t product, uint32_t subsystemProduct) const
{
	uint64_t key = ((uint64_t)vendor << 48) | ((uint64_t)product << 32)
		| ((uint64_t)subsystemVendor << 16) | subsystemProduct;
	KeyedNameMap::const_iterator it = fPciSubsystems.find(key);
	if (it == fPciSubsystems.end())
		return NULL;

	return it->second.c_str();
}


const char*
HwdataDatabase::PciClassCode(uint32_t classCode) const
{
	// Prefer the subclass name ("00  VGA compatible controller"),
	// fall back to the base class ("C 03  Display controller").
	uint32_t base = (classCode >> 16) & 0xff;
	uint32_t sub = (classCode >> 8) & 0xff;

	ClassMap::const_iterator it = fPciClasses.find((base << 8) | sub);
	if (it != fPciClasses.end())
		return it->second.c_str();

	it = fPciClasses.find(base);
	if (it == fPciClasses.end())
		return NULL;

	return it->second.c_str();
}


const char*
HwdataDatabase::UsbVendor(uint32_t vendor) const
{
	IdNameMap::const_iterator it = fUsbVendors.find(vendor);
	if (it == fUsbVendors.end())
		return NULL;

	return it->second.c_str();
}


const char*
HwdataDatabase::UsbProduct(uint32_t vendor, uint32_t product) const
{
	KeyedNameMap::const_iterator it = fUsbProducts.find(
		((uint64_t)vendor << 16) | product);
	if (it == fUsbProducts.end())
		return NULL;

	return it->second.c_str();
}


const char*
HwdataDatabase::UsbClassCode(uint32_t classCode) const
{
	ClassMap::const_iterator it = fUsbClasses.find(classCode & 0xff);
	if (it == fUsbClasses.end())
		return NULL;

	return it->second.c_str();
}


}	// namespace Hardware
