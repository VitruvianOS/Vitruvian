/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "EdidParser.h"

#include <stdio.h>
#include <string.h>


namespace Hardware {


static const uint8_t kEdidHeader[8] = {
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00
};


EdidInfo::EdidInfo()
	:
	valid(false),
	hasName(false),
	hasPreferredMode(false),
	hasSerialString(false),
	manufacturer(0),
	product(0),
	serial(0)
{
}


static void
trim_text(std::string& text)
{
	while (!text.empty() && (text[text.size() - 1] == '\n'
		|| text[text.size() - 1] == '\r' || text[text.size() - 1] == ' '))
		text.erase(text.size() - 1);

	size_t start = 0;
	while (start < text.size() && text[start] == ' ')
		start++;
	if (start > 0)
		text = text.substr(start);
}


bool
ParseEdid(const uint8_t* data, size_t length, EdidInfo& info)
{
	info = EdidInfo();

	if (data == NULL || length < 128)
		return false;

	if (memcmp(data, kEdidHeader, sizeof(kEdidHeader)) != 0)
		return false;

	info.valid = true;
	info.manufacturer = (uint16_t)((data[8] << 8) | data[9]);
	info.product = (uint16_t)(data[10] | (data[11] << 8));
	info.serial = (uint32_t)(data[12] | (data[13] << 8) | (data[14] << 16)
		| (data[15] << 24));

	// Four 18-byte descriptors start at offset 54.
	for (int i = 0; i < 4; i++) {
		const uint8_t* d = data + 54 + i * 18;
		uint16_t pixelClock = (uint16_t)(d[0] | (d[1] << 8));

		if (pixelClock != 0) {
			if (!info.hasPreferredMode) {
				int hactive = d[2] | ((d[4] & 0xf0) << 4);
				int vactive = d[5] | ((d[7] & 0xf0) << 4);
				if (hactive > 0 && vactive > 0) {
					char mode[32];
					snprintf(mode, sizeof(mode), "%dx%d", hactive,
						vactive);
					info.preferredMode = mode;
					info.hasPreferredMode = true;
				}
			}
			continue;
		}

		// Descriptor layout: bytes 0-1 pixel clock (zero here),
		// byte 2 reserved, byte 3 the tag, byte 4 reserved, then
		// 13 bytes of text.
		if (d[3] == 0xfc || d[3] == 0xff) {
			char text[14];
			memcpy(text, d + 5, 13);
			text[13] = '\0';
			std::string value(text);
			trim_text(value);

			if (d[3] == 0xfc && !value.empty()) {
				info.name = value;
				info.hasName = true;
			} else if (d[3] == 0xff && !value.empty())
				info.hasSerialString = true;
		}
	}

	return true;
}


}	// namespace Hardware
