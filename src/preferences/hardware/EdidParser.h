/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _EDID_PARSER_H
#define _EDID_PARSER_H


#include <stdint.h>

#include <string>
#include <vector>


// Minimal EDID 1.3 base-block reader: monitor name from the display
// product name descriptor, preferred timing as WxH, and the list of
// standard timings is left to the sysfs modes file.


namespace Hardware {


struct EdidInfo {
	bool		valid;
	bool		hasName;
	bool		hasPreferredMode;
	bool		hasSerialString;
	std::string	name;
	std::string	preferredMode;
	uint16_t		manufacturer;
	uint16_t		product;
	uint32_t		serial;

				EdidInfo();
};


bool
ParseEdid(const uint8_t* data, size_t length, EdidInfo& info);


}	// namespace Hardware


#endif	// _EDID_PARSER_H
