/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * gpsd TPV/SKY JSON and NMEA GGA/RMC/GSA/GSV field extraction.
 */
#ifndef GPSD_PARSER_H
#define GPSD_PARSER_H

#include <String.h>
#include <SupportDefs.h>


struct GpsdFix {
	double		latitude;
	double		longitude;
	double		altitude;
	double		accuracy;
	double		speed;
	double		heading;
	int32		fix;
	int32		satellitesUsed;
	int32		satellitesInView;
	BString		source;
	BString		device;
	bool		valid;
};


struct GpsdSourceStatus {
	bool		connected;
	// True when the report explicitly states the fix state (also fix == 0),
	// so a receiver losing its fix is not mistaken for "no news".
	bool		fixKnown;
	BString		error;
	BString		device;
	BString		source;
	int32		fix;
	int32		satellitesUsed;
	int32		satellitesInView;
	double		accuracy;
};


// One gpsd line: JSON object or NMEA sentence. Fills fix and/or status;
// device is updated when the line names a device path.
bool GpsdParseLine(const BString& line, GpsdFix* fix,
	GpsdSourceStatus* status, BString* device);

bool GpsdParseJson(const BString& json, GpsdFix* fix,
	GpsdSourceStatus* status, BString* device);

bool GpsdParseNmea(const BString& sentence, GpsdFix* fix,
	GpsdSourceStatus* status, BString* device);


#endif	// GPSD_PARSER_H
