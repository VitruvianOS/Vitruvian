/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "LocationProvider.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <Message.h>
#include <Messenger.h>
#include <NetworkAddress.h>
#include <OS.h>
#include <StringList.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "LocationProvider"


static const uint32 kMsgStart = 'LcSt';
static const uint32 kMsgStop = 'LcSp';
static const uint32 kMsgData = 'LcDt';


// --- tiny JSON field extractors -------------------------------------------
// gpsd emits flat TPV/SKY objects; we only need a few keys, so a linear
// scan beats pulling a JSON library into the tree for a prototype.

static bool
_JsonFindKey(const BString& json, const char* key, const char** valueStart)
{
	BString pattern("\"");
	pattern << key << "\"";
	int32 index = json.FindFirst(pattern.String());
	if (index < 0)
		return false;

	index += pattern.Length();
	// skip spaces and the colon
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


// --- NMEA helpers ----------------------------------------------------------

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


// hhmmss.ss -> seconds since midnight
static double
_NmeaTimeToSeconds(const char* field)
{
	if (field == NULL || *field == '\0')
		return 0.0;

	double raw = strtod(field, NULL);
	int hours = (int)(raw / 10000.0);
	int minutes = (int)((raw - hours * 10000.0) / 100.0);
	double seconds = raw - hours * 10000.0 - minutes * 100.0;
	return hours * 3600.0 + minutes * 60.0 + seconds;
}


// --- LocationProvider ------------------------------------------------------

LocationProvider::LocationProvider(const char* host, uint16 port)
	:
	BLooper("LocationProvider"),
	fHost(host != NULL ? host : "127.0.0.1"),
	fPort(port),
	fThread(-1),
	fEnabled(false),
	fConnected(false),
	fQuitRequested(false),
	fSourceName("gpsd"),
	fAccuracyLevel(B_LOCATION_ACCURACY_EXACT),
	fFix(0),
	fSatellitesInView(0),
	fSatellitesInUse(0),
	fLatitude(0.0),
	fLongitude(0.0),
	fAltitude(0.0),
	fAccuracy(0.0),
	fSpeed(0.0),
	fHeading(0.0),
	fTimestamp(0),
	fHavePosition(false)
{
}


LocationProvider::~LocationProvider()
{
	Stop();
}


void
LocationProvider::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgStart:
		{
			status_t status = _Connect();
			if (status != B_OK) {
				_NotifyStatus();
				break;
			}

			fThread = spawn_thread(_ThreadEntry, "gpsd reader",
				B_NORMAL_PRIORITY, this);
			if (fThread < 0) {
				_SetError("could not spawn gpsd reader");
				_Disconnect();
				_NotifyStatus();
				break;
			}
			resume_thread(fThread);
			fEnabled = true;
			_NotifyStatus();
			break;
		}
		case kMsgStop:
		{
			Stop();
			_NotifyStatus();
			break;
		}
		case kMsgData:
		{
			// Internal: the reader thread posts parsed updates here so
			// watchers are notified from a single thread.
			_Publish();
			break;
		}
		default:
			BLooper::MessageReceived(message);
			break;
	}
}


void
LocationProvider::Quit()
{
	fQuitRequested = true;
	Stop();
	BLooper::Quit();
}


status_t
LocationProvider::Start()
{
	if (fEnabled)
		return B_OK;

	fQuitRequested = false;
	return PostMessage(kMsgStart);
}


void
LocationProvider::Stop()
{
	fEnabled = false;

	if (fThread >= 0) {
		fQuitRequested = true;
		// Unblock the reader by disconnecting the socket underneath it.
		fSocket.Disconnect();
		int32 state = 0;
		thread_id thread = fThread;
		fThread = -1;
		wait_for_thread(thread, &state);
	} else {
		fQuitRequested = true;
	}

	_Disconnect();
	fConnected = false;
}


status_t
LocationProvider::Watch(BMessenger target)
{
	if (!target.IsValid())
		return B_BAD_VALUE;

	fLock.Lock();
	int32 count = fWatchers.CountItems();
	for (int32 i = 0; i < count; i++) {
		BMessenger* existing = (BMessenger*)fWatchers.ItemAt(i);
		if (*existing == target) {
			fLock.Unlock();
			return B_OK;
		}
	}

	fWatchers.AddItem(new BMessenger(target));
	fLock.Unlock();

	// Hand the new watcher the current status immediately.
	_NotifyStatus();
	return B_OK;
}


status_t
LocationProvider::Watch(BHandler* handler)
{
	if (handler == NULL)
		return B_BAD_VALUE;
	return Watch(BMessenger(handler));
}


status_t
LocationProvider::Unwatch(BMessenger target)
{
	fLock.Lock();
	int32 count = fWatchers.CountItems();
	for (int32 i = 0; i < count; i++) {
		BMessenger* existing = (BMessenger*)fWatchers.ItemAt(i);
		if (*existing == target) {
			delete (BMessenger*)fWatchers.RemoveItem(i);
			break;
		}
	}
	fLock.Unlock();
	return B_OK;
}


status_t
LocationProvider::Unwatch(BHandler* handler)
{
	if (handler == NULL)
		return B_BAD_VALUE;
	return Unwatch(BMessenger(handler));
}


int32
LocationProvider::FixType() const
{
	return fFix;
}


int32
LocationProvider::SatellitesInView() const
{
	return fSatellitesInView;
}


int32
LocationProvider::SatellitesInUse() const
{
	return fSatellitesInUse;
}


void
LocationProvider::LastLocation(double* latitude, double* longitude,
	double* altitude, double* accuracy, double* speed, double* heading,
	int64* timestamp) const
{
	fLock.Lock();
	if (latitude != NULL)
		*latitude = fLatitude;
	if (longitude != NULL)
		*longitude = fLongitude;
	if (altitude != NULL)
		*altitude = fAltitude;
	if (accuracy != NULL)
		*accuracy = fAccuracy;
	if (speed != NULL)
		*speed = fSpeed;
	if (heading != NULL)
		*heading = fHeading;
	if (timestamp != NULL)
		*timestamp = fTimestamp;
	fLock.Unlock();
}


void
LocationProvider::SetAccuracyLevel(int32 level)
{
	if (level < B_LOCATION_ACCURACY_EXACT || level > B_LOCATION_ACCURACY_ROUGH)
		level = B_LOCATION_ACCURACY_EXACT;
	fAccuracyLevel = level;
}


void
LocationProvider::_NotifyStatus()
{
	BMessage message(B_LOCATION_STATUS);
	message.AddInt32("status", fEnabled
		? (fConnected
			? (fFix >= 3 ? B_LOCATION_3D_FIX
				: fFix == 2 ? B_LOCATION_2D_FIX
				: B_LOCATION_NO_FIX)
			: B_LOCATION_CONNECTING)
		: B_LOCATION_OFFLINE);
	message.AddString("error", fLastError.String());
	message.AddString("device", fDevicePath.String());
	message.AddString("source", fSourceName.String());
	message.AddInt32("satellites", fSatellitesInUse);
	message.AddInt32("satellites_view", fSatellitesInView);
	message.AddInt32("fix", fFix);
	message.AddBool("enabled", fEnabled);
	message.AddBool("connected", fConnected);

	fLock.Lock();
	BList copy(fWatchers);
	fLock.Unlock();

	int32 count = copy.CountItems();
	for (int32 i = 0; i < count; i++) {
		BMessenger* target = (BMessenger*)copy.ItemAt(i);
		target->SendMessage(&message);
	}
}


void
LocationProvider::_Publish()
{
	if (!fHavePosition)
		return;

	BMessage update(B_LOCATION_CHANGED);

	double latitude = fLatitude;
	double longitude = fLongitude;
	double accuracy = fAccuracy;
	_ApplyAccuracy(&latitude, &longitude, &accuracy);

	update.AddDouble("latitude", latitude);
	update.AddDouble("longitude", longitude);
	update.AddDouble("altitude", fAltitude);
	update.AddDouble("accuracy", accuracy);
	update.AddDouble("speed", fSpeed);
	update.AddDouble("heading", fHeading);
	update.AddInt64("timestamp", fTimestamp);
	update.AddInt32("satellites", fSatellitesInUse);
	update.AddInt32("satellites_view", fSatellitesInView);
	update.AddInt32("fix", fFix);
	update.AddString("source", fSourceName.String());
	update.AddString("device", fDevicePath.String());

	fLock.Lock();
	BList copy(fWatchers);
	fLock.Unlock();

	int32 count = copy.CountItems();
	for (int32 i = 0; i < count; i++) {
		BMessenger* target = (BMessenger*)copy.ItemAt(i);
		target->SendMessage(&update);
	}
}


void
LocationProvider::_ApplyAccuracy(double* latitude, double* longitude,
	double* accuracy) const
{
	// Snap to coarser cells so the preflet can show "city" or "country"
	// without a second location service. ~1 km for city, ~50 km for rough.
	if (fAccuracyLevel == B_LOCATION_ACCURACY_EXACT)
		return;

	double cell = fAccuracyLevel == B_LOCATION_ACCURACY_APPROXIMATE
		? 0.01 : 0.5;
	*latitude = (double)((int64)(*latitude / cell)) * cell;
	*longitude = (double)((int64)(*longitude / cell)) * cell;
	if (accuracy != NULL && *accuracy < cell * 111000.0)
		*accuracy = cell * 111000.0;
}


bool
LocationProvider::_Connect()
{
	_Disconnect();
	fLastError = "";

	BNetworkAddress address;
	status_t status = address.SetTo(fHost.String(), fPort);
	if (status != B_OK) {
		_SetError("could not resolve gpsd host");
		return false;
	}

	fConnected = fSocket.Connect(address, 5 * 1000000LL) == B_OK;
	if (!fConnected) {
		_SetError("could not connect to gpsd");
		return false;
	}

	// Prefer JSON: flat objects, no binary framing. Fall back to raw NMEA
	// if gpsfake is replaying a plain log without WATCH support.
	const char* watch = "?WATCH={\"enable\":true,\"json\":true};\n";
	fSocket.Write(watch, strlen(watch));
	return true;
}


void
LocationProvider::_Disconnect()
{
	if (fSocket.InitCheck() == B_OK)
		fSocket.Disconnect();
	fConnected = false;
}


int32
LocationProvider::_ThreadEntry(void* data)
{
	LocationProvider* provider = (LocationProvider*)data;
	provider->_ReadLoop();
	return 0;
}


void
LocationProvider::_ReadLoop()
{
	char buffer[4096];
	BString pending;

	fConnected = true;
	_NotifyStatus();

	while (!fQuitRequested) {
		ssize_t bytes = fSocket.Read(buffer, sizeof(buffer) - 1);
		if (bytes <= 0) {
			if (fQuitRequested)
				break;
			_SetError("gpsd connection lost");
			fConnected = false;
			_NotifyStatus();
			break;
		}

		buffer[bytes] = '\0';
		pending << buffer;

		int32 index = pending.FindFirst('\n');
		while (index >= 0 && !fQuitRequested) {
			BString line;
			pending.CopyInto(line, 0, index);
			pending.Remove(0, index + 1);
			line.Trim();

			if (!line.IsEmpty())
				_HandleLine(line);

			index = pending.FindFirst('\n');
		}
	}

	fConnected = false;
}


void
LocationProvider::_HandleLine(const BString& line)
{
	// gpsd JSON objects and NMEA sentences both arrive as lines.
	if (line[0] == '{')
		_HandleJson(line);
	else if (line[0] == '$')
		_HandleNmea(line);
}


void
LocationProvider::_HandleJson(const BString& json)
{
	BString classString;
	if (!_JsonString(json, "class", &classString))
		return;

	if (classString == "DEVICES") {
		// Optionally extract the first device path from a nested object.
		const char* path = strstr(json.String(), "\"path\"");
		if (path != NULL) {
			BString pathJson(path);
			BString device;
			if (_JsonString(pathJson, "path", &device) && !device.IsEmpty())
				fDevicePath = device;
		}
		return;
	}

	if (classString == "DEVICE") {
		BString device;
		if (_JsonString(json, "path", &device) && !device.IsEmpty())
			fDevicePath = device;
		return;
	}

	if (classString == "TPV") {
		int32 mode = 0;
		_JsonInt(json, "mode", &mode);
		if (mode < 2)
			return;

		double value;
		if (_JsonDouble(json, "lat", &value))
			fLatitude = value;
		if (_JsonDouble(json, "lon", &value))
			fLongitude = value;
		if (_JsonDouble(json, "alt", &value))
			fAltitude = value;
		if (_JsonDouble(json, "speed", &value))
			fSpeed = value;
		if (_JsonDouble(json, "track", &value))
			fHeading = value;

		// Prefer epx/epy (1-sigma in metres); fall back to pdop-ish guess.
		double epx = 0.0;
		double epy = 0.0;
		bool hasEpx = _JsonDouble(json, "epx", &epx);
		bool hasEpy = _JsonDouble(json, "epy", &epy);
		if (hasEpx || hasEpy)
			fAccuracy = hasEpx && hasEpy ? sqrt(epx * epx + epy * epy) : epx;
		else if (_JsonDouble(json, "hdop", &value))
			fAccuracy = value * 5.0;	// rough metres, HDOP-only receivers
		else
			fAccuracy = 10.0;

		fTimestamp = real_time_clock() * 1000000LL;
		fHavePosition = true;
		_SetFix(mode >= 3 ? 3 : 2);

		if (fSourceName != "gpsd")
			fSourceName = "gpsd";
		if (fDevicePath.IsEmpty())
			fDevicePath = "gpsd";

		PostMessage(kMsgData);
		return;
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

		// Some gpsd builds report an explicit count.
		int32 explicitUsed = 0;
		if (_JsonInt(json, "satellites_used", &explicitUsed) && explicitUsed > 0)
			used = explicitUsed;

		fSatellitesInUse = used;
		fSatellitesInView = inView > used ? inView : used;
		_NotifyStatus();
		return;
	}
}


void
LocationProvider::_HandleNmea(const BString& sentence)
{
	BString type;
	BStringList fields;
	_SplitNmea(sentence, &type, &fields);
	if (type.Length() < 3)
		return;

	// Talker-independent: GGA/RMC/GSA/GSV (GP, GN, GL, GA, GB, ...).
	BString suffix = type.String() + 2;

	if (suffix == "GGA" && fields.CountStrings() >= 10) {
		double time = _NmeaTimeToSeconds(fields.StringAt(0));
		(void)time;
		fLatitude = _NmeaCoordToDegrees(fields.StringAt(1),
			fields.StringAt(2)[0]);
		fLongitude = _NmeaCoordToDegrees(fields.StringAt(3),
			fields.StringAt(4)[0]);
		int32 quality = atoi(fields.StringAt(5));
		fSatellitesInUse = atoi(fields.StringAt(6));
		if (quality > 0) {
			fAltitude = strtod(fields.StringAt(8), NULL);
			fAccuracy = 5.0;
			fTimestamp = real_time_clock() * 1000000LL;
			fHavePosition = true;
			_SetFix(fAltitude > 0.0 ? 3 : 2);
			if (fSourceName != "gpsd-nmea")
				fSourceName = "gpsd-nmea";
			if (fDevicePath.IsEmpty())
				fDevicePath = "nmea";
			PostMessage(kMsgData);
		}
		return;
	}

	if (suffix == "RMC" && fields.CountStrings() >= 9) {
		char status = fields.StringAt(1)[0];
		if (status != 'A')
			return;

		fLatitude = _NmeaCoordToDegrees(fields.StringAt(2),
			fields.StringAt(3)[0]);
		fLongitude = _NmeaCoordToDegrees(fields.StringAt(4),
			fields.StringAt(5)[0]);
		// knots -> m/s
		fSpeed = strtod(fields.StringAt(6), NULL) * 0.514444;
		fHeading = strtod(fields.StringAt(7), NULL);
		fAccuracy = 5.0;
		fTimestamp = real_time_clock() * 1000000LL;
		fHavePosition = true;
		if (fFix < 2)
			_SetFix(2);
		if (fSourceName != "gpsd-nmea")
			fSourceName = "gpsd-nmea";
		if (fDevicePath.IsEmpty())
			fDevicePath = "nmea";
		PostMessage(kMsgData);
		return;
	}

	if (suffix == "GSA") {
		// Fields: talker, mode, fix, then 12 sat IDs, PDOP, HDOP, VDOP.
		int32 fix = atoi(fields.StringAt(1));
		if (fix >= 2) {
			_SetFix(fix);
			if (fields.CountStrings() >= 16) {
				double hdop = strtod(fields.StringAt(14), NULL);
				double vdop = strtod(fields.StringAt(15), NULL);
				fAccuracy = sqrt(hdop * hdop + vdop * vdop) * 5.0;
			}
		}
		return;
	}

	if (suffix == "GSV") {
		// Fields: talker, total messages, message number, sats in view,
		// then groups of 4 (PRN, elevation, azimuth, SNR).
		// After type strip: fields[0]=total, [1]=msg#, [2]=sats in view.
		int32 totalMessages = atoi(fields.StringAt(0));
		int32 messageNumber = atoi(fields.StringAt(1));
		int32 satsInView = atoi(fields.StringAt(2));
		if (messageNumber == 1 && satsInView > 0)
			fSatellitesInView = satsInView;

		// Count used satellites is not in GSV; leave InUse from GGA/GSA.
		if (totalMessages > 0 && messageNumber == totalMessages)
			_NotifyStatus();
		return;
	}
}


void
LocationProvider::_SplitNmea(const BString& sentence, BString* type,
	BStringList* fields) const
{
	// Strip checksum: keep up to '*'
	BString body(sentence);
	int32 star = body.FindFirst('*');
	if (star >= 0)
		body.Truncate(star);

	if (!body.StartsWith("$"))
		return;

	body.Remove(0, 1);
	// Sentence type is up to the first comma (or end)
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


void
LocationProvider::_SetFix(int32 fix)
{
	if (fFix == fix)
		return;
	fFix = fix;
	_NotifyStatus();
}


void
LocationProvider::_SetError(const char* error)
{
	fLastError = error != NULL ? error : "";
}
