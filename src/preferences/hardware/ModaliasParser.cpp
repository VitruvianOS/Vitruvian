/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "ModaliasParser.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <utility>
#include <vector>


namespace Hardware {


ModaliasInfo::ModaliasInfo()
	:
	hasVendor(false),
	hasDevice(false),
	hasSubVendor(false),
	hasSubDevice(false),
	hasClass(false),
	vendor(0),
	device(0),
	subVendor(0),
	subDevice(0),
	classCode(0)
{
}


static bool
is_wildcard(const std::string& value)
{
	if (value.empty())
		return true;

	for (size_t i = 0; i < value.size(); i++) {
		if (value[i] != '*' && value[i] != 'X' && value[i] != 'x')
			return false;
	}

	return true;
}


static bool
parse_hex_value(const std::string& value, uint32_t& out)
{
	if (is_wildcard(value))
		return false;

	char* end = NULL;
	unsigned long parsed = strtoul(value.c_str(), &end, 16);
	if (end == NULL || *end != '\0')
		return false;

	out = (uint32_t)parsed;
	return true;
}


// Collects "<letters><hex-or-wildcard>" tokens; modalias field prefixes
// are letter runs (v, d, sv, dc, isc, ...) so a generic scanner handles
// every bus without a per-prefix table.
static void
scan_fields(const std::string& text,
	std::vector<std::pair<std::string, std::string> >& fields)
{
	size_t i = 0;
	while (i < text.size()) {
		size_t keyStart = i;
		while (i < text.size() && isalpha((unsigned char)text[i]))
			i++;
		if (i == keyStart) {
			i++;
			continue;
		}

		std::string key = text.substr(keyStart, i - keyStart);

		size_t valueStart = i;
		while (i < text.size()
			&& (isxdigit((unsigned char)text[i]) || text[i] == '*'
				|| text[i] == 'X' || text[i] == 'x'))
			i++;

		std::string value = text.substr(valueStart, i - valueStart);
		fields.push_back(std::make_pair(key, value));
	}
}


// Fixed-width field tables per bus. The generic scanner cannot be used
// for pci/usb: field values are hex and the next key often starts with
// a hex digit (d, e, f, b, c, a), so unconsumed values swallow it.
struct FieldSpec {
	const char*		key;
	int				width;
};


static const FieldSpec kPciFields[] = {
	{ "sv", 8 }, { "sd", 8 }, { "v", 8 }, { "d", 8 },
	{ "bc", 2 }, { "sc", 2 }, { "i", 2 },
	{ NULL, 0 }
};


static const FieldSpec kUsbFields[] = {
	{ "dc", 2 }, { "dp", 2 }, { "ic", 2 }, { "ip", 2 }, { "in", 2 },
	{ "v", 4 }, { "p", 4 }, { "d", 4 },
	{ NULL, 0 }
};


static const FieldSpec kSdioFields[] = {
	{ "c", 2 }, { "v", 4 }, { "p", 4 },
	{ NULL, 0 }
};


static const FieldSpec kInputFields[] = {
	{ "b", 4 }, { "v", 4 }, { "p", 4 }, { "e", 4 },
	{ NULL, 0 }
};


static bool
all_hex(const std::string& value)
{
	if (value.empty())
		return false;

	for (size_t i = 0; i < value.size(); i++) {
		if (!isxdigit((unsigned char)value[i]))
			return false;
	}

	return true;
}


static void
scan_width_fields(const std::string& text, const FieldSpec* spec,
	std::vector<std::pair<std::string, std::string> >& fields)
{
	size_t i = 0;
	while (i < text.size()) {
		const FieldSpec* match = NULL;
		for (const FieldSpec* field = spec; field->key != NULL; field++) {
			size_t length = strlen(field->key);
			if (text.compare(i, length, field->key) == 0) {
				match = field;
				break;
			}
		}

		if (match == NULL) {
			i++;
			continue;
		}

		i += strlen(match->key);

		// A wildcard field is one or more '*'.
		size_t wildcard = i;
		while (wildcard < text.size() && text[wildcard] == '*')
			wildcard++;
		if (wildcard > i) {
			fields.push_back(std::make_pair(match->key,
				std::string("*")));
			i = wildcard;
			continue;
		}

		if (i + match->width > text.size())
			break;

		std::string value = text.substr(i, match->width);
		if (!all_hex(value))
			break;

		fields.push_back(std::make_pair(match->key, value));
		i += match->width;
	}
}


static void
apply_field(ModaliasInfo& info, const std::string& key,
	const std::string& value)
{
	uint32_t parsed = 0;

	if (key == "v" || key == "ven") {
		if (parse_hex_value(value, parsed)) {
			info.vendor = parsed;
			info.hasVendor = true;
		}
	} else if (key == "d" || key == "dev") {
		// A usb "d" is bcdDevice and must not clobber the product id.
		if (!info.hasDevice && parse_hex_value(value, parsed)) {
			info.device = parsed;
			info.hasDevice = true;
		}
	} else if (key == "p") {
		// usb/input spell the device id "p".
		if (parse_hex_value(value, parsed)) {
			info.device = parsed;
			info.hasDevice = true;
		}
	} else if (key == "sv") {
		if (parse_hex_value(value, parsed)) {
			info.subVendor = parsed;
			info.hasSubVendor = true;
		}
	} else if (key == "sd") {
		if (parse_hex_value(value, parsed)) {
			info.subDevice = parsed;
			info.hasSubDevice = true;
		}
	} else if (key == "bc" || key == "c" || key == "dc") {
		// bc is pci base class, dc is the usb device class; both
		// feed the base class byte.
		if (parse_hex_value(value, parsed)) {
			info.classCode |= (parsed & 0xff) << 16;
			info.hasClass = true;
		}
	} else if (key == "sc" || key == "dp") {
		if (parse_hex_value(value, parsed))
			info.classCode |= (parsed & 0xff) << 8;
	} else if (key == "i" || key == "ic" || key == "ip" || key == "in") {
		if (parse_hex_value(value, parsed))
			info.classCode |= parsed & 0xff;
	}
}


static void
split_compatibles(const std::string& text,
	std::vector<std::string>& out)
{
	// The kernel separates multiple compatibles with NUL bytes; the
	// commas inside a compatible ("vendor,chip") are part of the name.
	size_t start = 0;
	while (start < text.size()) {
		size_t end = text.find('\0', start);
		if (end == std::string::npos)
			end = text.size();

		if (end > start)
			out.push_back(text.substr(start, end - start));

		start = end + 1;
	}
}


static void
parse_of(const std::string& remainder, ModaliasInfo& info)
{
	// of:N<name>*T<type>*C<compat>[,<compat>...]
	size_t nameStart = remainder.find('N');
	size_t typeStart = remainder.find('T');
	size_t compatStart = remainder.find('C');

	if (nameStart != std::string::npos && typeStart != std::string::npos
		&& typeStart > nameStart) {
		info.name = remainder.substr(nameStart + 1,
			typeStart - nameStart - 1);
		while (!info.name.empty()
			&& info.name[info.name.size() - 1] == '*')
			info.name.erase(info.name.size() - 1);
	}

	if (compatStart != std::string::npos) {
		std::string compat = remainder.substr(compatStart + 1);
		split_compatibles(compat, info.ofCompatibles);
		if (!info.ofCompatibles.empty())
			info.name = info.ofCompatibles[0];
	}
}


bool
ParseModalias(const std::string& text, ModaliasInfo& info)
{
	info = ModaliasInfo();

	size_t colon = text.find(':');
	if (colon == std::string::npos || colon == 0)
		return false;

	info.raw = text;
	info.bus = text.substr(0, colon);
	std::string remainder = text.substr(colon + 1);

	if (info.bus == "of") {
		parse_of(remainder, info);
		return true;
	}

	if (info.bus == "acpi" || info.bus == "platform" || info.bus == "i2c"
		|| info.bus == "spi" || info.bus == "sdio" || info.bus == "serio"
		|| info.bus == "input" || info.bus == "mmc" || info.bus == "nvme") {
		// Named devices: the whole remainder is the name (input,
		// serio and sdio still carry id fields, parsed below).
		std::vector<std::pair<std::string, std::string> > fields;
		if (info.bus == "input")
			scan_width_fields(remainder, kInputFields, fields);
		else if (info.bus == "sdio")
			scan_width_fields(remainder, kSdioFields, fields);
		else if (info.bus == "serio")
			scan_fields(remainder, fields);
		else {
			info.name = remainder;
			return true;
		}

		for (size_t i = 0; i < fields.size(); i++)
			apply_field(info, fields[i].first, fields[i].second);

		// input keeps the "b0003v....p....e...." shape; the whole
		// remainder doubles as the name.
		info.name = remainder;
		return true;
	}

	// pci, usb, virtio and friends: letter-prefixed id fields of a
	// known width.
	std::vector<std::pair<std::string, std::string> > fields;
	if (info.bus == "usb")
		scan_width_fields(remainder, kUsbFields, fields);
	else if (info.bus == "pci" || info.bus == "virtio")
		scan_width_fields(remainder, kPciFields, fields);
	else
		scan_fields(remainder, fields);

	for (size_t i = 0; i < fields.size(); i++)
		apply_field(info, fields[i].first, fields[i].second);

	return true;
}


}	// namespace Hardware
