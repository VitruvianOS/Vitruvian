/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * gpsd TPV/SKY JSON and NMEA GGA/RMC/GSA/GSV field extraction.
 */

#include "GpsdParser.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <StringList.h>
#include <SupportDefs.h>


// gpsd emits flat TPV/SKY objects, so a linear scan for a few keys is enough.

static bool
_JsonFindKey(const BString& json, const char* key, const char** valueStart)
{
	BString pattern("\"");
	pattern << key << "\"";
	int32 index = json.FindFirst(pattern.String());
	if (index < 0)
		return false;

	index += pattern.Length();
	while (index < json.Length() && (json[index] == ' '
		|| json[index] == '\t' || json[index] == ':')) {
		index++;
	}
	if (index >= json.Length())
		return false;

	*valueStart = json.String() + index;
	return true;
}


static bool
_JsonDouble(const BString& json, const char* key, double* value)
{
	const char* start;
	if (!_JsonFindKey(json, key, &start))
		return false;

	char* end = NULL;
	double result = strtod(start, &end);
	if (end == start)
		return false;
	*value = result;
	return true;
}


static bool
_JsonInt(const BString& json, const char* key, int32* value)
{
	const char* start;
	if (!_JsonFindKey(json, key, &start))
		return false;

	char* end = NULL;
	long result = strtol(start, &end, 10);
	if (end == start)
		return false;
	*value = (int32)result;
	return true;
}


static bool
_JsonString(const BString& json, const char* key, BString* value)
{
	const char* start;
	if (!_JsonFindKey(json, key, &start))
		return false;

	if (*start != '"')
		return false;
	start++;

	const char* end = strchr(start, '"');
	if (end == NULL)
		return false;

	value->SetTo(start, (int32)(end - start));
	return true;
}


// ddmm.mmmm -> degrees
static double
_NmeaCoordToDegrees(const char* field, char hemisphere)
{
	if (field == NULL || *field == '\0')
		return 0.0;

	double raw = strtod(field, NULL);
	int degrees = (int)(raw / 100.0);
	double minutes = raw - degrees * 100.0;
	double result = degrees + minutes / 60.0;

	if (hemisphere == 'S' || hemisphere == 'W')
		result = -result;
	return result;
}


static void
_SplitNmea(const BString& sentence, BString* type, BStringList* fields)
{
	// Strip checksum: keep up to '*'
	BString body(sentence);
	int32 star = body.FindFirst('*');
	if (star >= 0)
		body.Truncate(star);

	if (!body.StartsWith("$"))
		return;

	body.Remove(0, 1);
	int32 comma = body.FindFirst(',');
	if (comma >= 0) {
		body.CopyInto(*type, 0, comma);
		body.Remove(0, comma + 1);
	} else {
		*type = body;
		body = "";
	}

	BString remainder(body);
	int32 index = remainder.FindFirst(',');
	while (index >= 0) {
		BString field;
		remainder.CopyInto(field, 0, index);
		fields->Add(field);
		remainder.Remove(0, index + 1);
		index = remainder.FindFirst(',');
	}
	if (!remainder.IsEmpty())
		fields->Add(remainder);
}


static void
_ResetFix(GpsdFix* fix)
{
	if (fix == NULL)
		return;
	fix->latitude = 0.0;
	fix->longitude = 0.0;
	fix->altitude = 0.0;
	fix->accuracy = 0.0;
	fix->speed = 0.0;
	fix->heading = 0.0;
	fix->fix = 0;
	fix->satellitesUsed = 0;
	fix->satellitesInView = 0;
	fix->source = "";
	fix->device = "";
	fix->valid = false;
}


static void
_ResetStatus(GpsdSourceStatus* status)
{
	if (status == NULL)
		return;
	status->connected = false;
	status->error = "";
	status->device = "";
	status->source = "";
	status->fix = 0;
	status->satellitesUsed = 0;
	status->satellitesInView = 0;
	status->accuracy = 0.0;
}


bool
GpsdParseJson(const BString& json, GpsdFix* fix, GpsdSourceStatus* status,
	BString* device)
{
	_ResetFix(fix);
	_ResetStatus(status);

	BString classString;
	if (!_JsonString(json, "class", &classString))
		return false;

	if (classString == "DEVICES") {
		const char* path = strstr(json.String(), "\"path\"");
		if (path != NULL) {
			BString pathJson(path);
			BString found;
			if (_JsonString(pathJson, "path", &found) && !found.IsEmpty()) {
				if (device != NULL)
					*device = found;
				if (status != NULL) {
					status->connected = true;
					status->device = found;
					status->source = "gpsd";
				}
				return true;
			}
		}
		return false;
	}

	if (classString == "DEVICE") {
		BString found;
		if (_JsonString(json, "path", &found) && !found.IsEmpty()) {
			if (device != NULL)
				*device = found;
			if (status != NULL) {
				status->connected = true;
				status->device = found;
				status->source = "gpsd";
			}
			return true;
		}
		return false;
	}

	if (classString == "TPV") {
		int32 mode = 0;
		_JsonInt(json, "mode", &mode);
		if (mode < 2 || fix == NULL)
			return false;

		double latitude = 0.0;
		double longitude = 0.0;
		double altitude = 0.0;
		double speed = 0.0;
		double heading = 0.0;
		double value;
		_JsonDouble(json, "lat", &latitude);
		_JsonDouble(json, "lon", &longitude);
		_JsonDouble(json, "alt", &altitude);
		_JsonDouble(json, "speed", &speed);
		_JsonDouble(json, "track", &heading);

		// Prefer epx/epy (1-sigma in metres); fall back to a HDOP guess.
		double accuracy;
		double epx = 0.0;
		double epy = 0.0;
		bool hasEpx = _JsonDouble(json, "epx", &epx);
		bool hasEpy = _JsonDouble(json, "epy", &epy);
		if (hasEpx || hasEpy)
			accuracy = hasEpx && hasEpy ? sqrt(epx * epx + epy * epy) : epx;
		else if (_JsonDouble(json, "hdop", &value))
			accuracy = value * 5.0;
		else
			accuracy = 10.0;

		fix->latitude = latitude;
		fix->longitude = longitude;
		fix->altitude = altitude;
		fix->accuracy = accuracy;
		fix->speed = speed;
		fix->heading = heading;
		fix->fix = mode >= 3 ? 3 : 2;
		fix->source = "gpsd";
		if (device != NULL && !device->IsEmpty())
			fix->device = *device;
		else
			fix->device = "gpsd";
		fix->valid = true;
		return true;
	}

	if (classString == "SKY") {
		int32 used = 0;
		int32 inView = 0;

		// Count "used":true entries; satellites in view is the array length
		// (approximated by counting PRN fields when the array is present).
		const char* scan = json.String();
		while (scan != NULL && *scan != '\0') {
			const char* usedHit = strstr(scan, "\"used\":");
			if (usedHit != NULL) {
				if (usedHit[7] == 't' || usedHit[7] == 'T')
					used++;
				scan = usedHit + 7;
			} else {
				scan = NULL;
			}
		}

		scan = json.String();
		while (scan != NULL && *scan != '\0') {
			const char* prn = strstr(scan, "\"PRN\":");
			if (prn != NULL) {
				inView++;
				scan = prn + 6;
			} else {
				scan = NULL;
			}
		}

		int32 explicitUsed = 0;
		if (_JsonInt(json, "satellites_used", &explicitUsed) && explicitUsed > 0)
			used = explicitUsed;

		if (status != NULL) {
			status->connected = true;
			status->satellitesUsed = used;
			status->satellitesInView = inView > used ? inView : used;
			status->source = "gpsd";
			if (device != NULL)
				status->device = *device;
		}
		return true;
	}

	return false;
}


bool
GpsdParseNmea(const BString& sentence, GpsdFix* fix, GpsdSourceStatus* status,
	BString* device)
{
	_ResetFix(fix);
	_ResetStatus(status);

	BString type;
	BStringList fields;
	_SplitNmea(sentence, &type, &fields);
	if (type.Length() < 3)
		return false;

	// Talker-independent: GGA/RMC/GSA/GSV (GP, GN, GL, GA, GB, ...).
	BString suffix = type.String() + 2;

	if (suffix == "GGA" && fields.CountStrings() >= 10) {
		double latitude = _NmeaCoordToDegrees(fields.StringAt(1),
			fields.StringAt(2)[0]);
		double longitude = _NmeaCoordToDegrees(fields.StringAt(3),
			fields.StringAt(4)[0]);
		int32 quality = atoi(fields.StringAt(5));
		int32 satellites = atoi(fields.StringAt(6));
		if (quality > 0 && fix != NULL) {
			double altitude = strtod(fields.StringAt(8), NULL);
			fix->latitude = latitude;
			fix->longitude = longitude;
			fix->altitude = altitude;
			fix->accuracy = 5.0;
			fix->fix = altitude > 0.0 ? 3 : 2;
			fix->satellitesUsed = satellites;
			fix->source = "gpsd-nmea";
			if (device != NULL && !device->IsEmpty())
				fix->device = *device;
			else
				fix->device = "nmea";
			fix->valid = true;
			return true;
		}
		if (status != NULL) {
			status->connected = true;
			status->satellitesUsed = satellites;
			status->source = "gpsd-nmea";
		}
		return false;
	}

	if (suffix == "RMC" && fields.CountStrings() >= 9) {
		char statusFlag = fields.StringAt(1)[0];
		if (statusFlag != 'A' || fix == NULL)
			return false;

		fix->latitude = _NmeaCoordToDegrees(fields.StringAt(2),
			fields.StringAt(3)[0]);
		fix->longitude = _NmeaCoordToDegrees(fields.StringAt(4),
			fields.StringAt(5)[0]);
		fix->speed = strtod(fields.StringAt(6), NULL) * 0.514444;
		fix->heading = strtod(fields.StringAt(7), NULL);
		fix->accuracy = 5.0;
		fix->fix = 2;
		fix->source = "gpsd-nmea";
		if (device != NULL && !device->IsEmpty())
			fix->device = *device;
		else
			fix->device = "nmea";
		fix->valid = true;
		return true;
	}

	if (suffix == "GSA") {
		// Fields: talker, mode, fix, then 12 sat IDs, PDOP, HDOP, VDOP.
		int32 fixType = atoi(fields.StringAt(1));
		if (fixType >= 2 && status != NULL) {
			double accuracy = 0.0;
			if (fields.CountStrings() >= 16) {
				double hdop = strtod(fields.StringAt(14), NULL);
				double vdop = strtod(fields.StringAt(15), NULL);
				accuracy = sqrt(hdop * hdop + vdop * vdop) * 5.0;
			}
			status->connected = true;
			status->fix = fixType;
			status->accuracy = accuracy;
			status->source = "gpsd-nmea";
		}
		return fixType >= 2;
	}

	if (suffix == "GSV") {
		// Fields: talker, total messages, message number, sats in view,
		// then groups of 4 (PRN, elevation, azimuth, SNR).
		int32 totalMessages = atoi(fields.StringAt(0));
		int32 messageNumber = atoi(fields.StringAt(1));
		int32 satsInView = atoi(fields.StringAt(2));
		if (status != NULL
			&& ((messageNumber == 1 && satsInView > 0)
				|| (totalMessages > 0 && messageNumber == totalMessages))) {
			status->connected = true;
			status->satellitesInView = satsInView;
			status->source = "gpsd-nmea";
		}
		return satsInView > 0;
	}

	return false;
}


bool
GpsdParseLine(const BString& line, GpsdFix* fix, GpsdSourceStatus* status,
	BString* device)
{
	if (line.IsEmpty())
		return false;

	if (line[0] == '{')
		return GpsdParseJson(line, fix, status, device);
	if (line[0] == '$')
		return GpsdParseNmea(line, fix, status, device);
	return false;
}
