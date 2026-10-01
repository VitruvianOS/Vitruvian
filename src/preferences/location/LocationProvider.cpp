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
static const uint32 kMsgStatus = 'LcSs';


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
			fLock.Lock();
			if (fEnabled && fThread >= 0 && fConnected) {
				fLock.Unlock();
				break;
			}
			fLock.Unlock();

			// Reconnect path: join any previous reader before a new one.
			Stop();
			fLock.Lock();
			fQuitRequested = false;
			fLock.Unlock();

			if (_Connect() != B_OK) {
				_NotifyStatus();
				break;
			}

			fLock.Lock();
			fThread = spawn_thread(_ThreadEntry, "gpsd reader",
				B_NORMAL_PRIORITY, this);
			if (fThread < 0) {
				fLastError = "could not spawn gpsd reader";
				fConnected = false;
				fLock.Unlock();
				if (fSocket.InitCheck() == B_OK)
					fSocket.Disconnect();
				_NotifyStatus();
				break;
			}
			thread_id thread = fThread;
			fLock.Unlock();
			resume_thread(thread);

			fLock.Lock();
			fEnabled = true;
			fConnected = true;
			fLock.Unlock();
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
			// Reader thread posted a parsed position snapshot; apply it
			// on the looper so watchers and getters see one owner.
			fLock.Lock();
			double value;
			if (message->FindDouble("latitude", &value) == B_OK)
				fLatitude = value;
			if (message->FindDouble("longitude", &value) == B_OK)
				fLongitude = value;
			if (message->FindDouble("altitude", &value) == B_OK)
				fAltitude = value;
			if (message->FindDouble("accuracy", &value) == B_OK)
				fAccuracy = value;
			if (message->FindDouble("speed", &value) == B_OK)
				fSpeed = value;
			if (message->FindDouble("heading", &value) == B_OK)
				fHeading = value;

			int32 intValue;
			if (message->FindInt32("fix", &intValue) == B_OK)
				fFix = intValue;
			if (message->FindInt32("satellites", &intValue) == B_OK)
				fSatellitesInUse = intValue;
			if (message->FindInt32("satellites_view", &intValue) == B_OK)
				fSatellitesInView = intValue;

			BString text;
			if (message->FindString("source", &text) == B_OK)
				fSourceName = text;
			if (message->FindString("device", &text) == B_OK
				&& !text.IsEmpty())
				fDevicePath = text;

			int64 timeValue;
			if (message->FindInt64("timestamp", &timeValue) == B_OK)
				fTimestamp = timeValue;
			else
				fTimestamp = real_time_clock() * 1000000LL;

			fHavePosition = true;
			fLock.Unlock();
			_Publish();
			break;
		}
		case kMsgStatus:
		{
			fLock.Lock();
			bool flag;
			if (message->FindBool("connected", &flag) == B_OK)
				fConnected = flag;

			BString text;
			if (message->FindString("error", &text) == B_OK)
				fLastError = text;
			if (message->FindString("device", &text) == B_OK
				&& !text.IsEmpty())
				fDevicePath = text;
			if (message->FindString("source", &text) == B_OK
				&& !text.IsEmpty())
				fSourceName = text;

			int32 intValue;
			if (message->FindInt32("fix", &intValue) == B_OK)
				fFix = intValue;
			if (message->FindInt32("satellites", &intValue) == B_OK)
				fSatellitesInUse = intValue;
			if (message->FindInt32("satellites_view", &intValue) == B_OK)
				fSatellitesInView = intValue;

			double accuracy;
			if (message->FindDouble("accuracy", &accuracy) == B_OK
				&& accuracy > 0.0)
				fAccuracy = accuracy;
			fLock.Unlock();
			_NotifyStatus();
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
	// Caller must hold Lock(); Quit deletes the looper on its own thread.
	Stop();
	BLooper::Quit();
}


status_t
LocationProvider::Start()
{
	fLock.Lock();
	if (fEnabled && fThread >= 0 && fConnected) {
		fLock.Unlock();
		return B_OK;
	}
	fQuitRequested = false;
	fLock.Unlock();
	return PostMessage(kMsgStart);
}


void
LocationProvider::Stop()
{
	// Safe from any thread. The reader never takes fLock (it only posts
	// snapshots), so waiting for it here cannot deadlock against the
	// looper holding fLock.
	fLock.Lock();
	fEnabled = false;
	fQuitRequested = true;

	thread_id thread = fThread;
	fThread = -1;
	fLock.Unlock();

	if (thread >= 0) {
		// Unblock the reader by disconnecting the socket underneath it.
		if (fSocket.InitCheck() == B_OK)
			fSocket.Disconnect();
		wait_for_thread(thread, NULL);
	} else if (fSocket.InitCheck() == B_OK) {
		fSocket.Disconnect();
	}

	fLock.Lock();
	fConnected = false;
	fLock.Unlock();
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


bool
LocationProvider::IsEnabled() const
{
	fLock.Lock();
	bool enabled = fEnabled;
	fLock.Unlock();
	return enabled;
}


bool
LocationProvider::IsConnected() const
{
	fLock.Lock();
	bool connected = fConnected;
	fLock.Unlock();
	return connected;
}


int32
LocationProvider::FixType() const
{
	fLock.Lock();
	int32 fix = fFix;
	fLock.Unlock();
	return fix;
}


int32
LocationProvider::SatellitesInView() const
{
	fLock.Lock();
	int32 satellites = fSatellitesInView;
	fLock.Unlock();
	return satellites;
}


int32
LocationProvider::SatellitesInUse() const
{
	fLock.Lock();
	int32 satellites = fSatellitesInUse;
	fLock.Unlock();
	return satellites;
}


const char*
LocationProvider::DevicePath() const
{
	fLock.Lock();
	const char* path = fDevicePath.String();
	fLock.Unlock();
	return path;
}


const char*
LocationProvider::SourceName() const
{
	fLock.Lock();
	const char* source = fSourceName.String();
	fLock.Unlock();
	return source;
}


const char*
LocationProvider::LastError() const
{
	fLock.Lock();
	const char* error = fLastError.String();
	fLock.Unlock();
	return error;
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
	fLock.Lock();
	fAccuracyLevel = level;
	fLock.Unlock();
}


int32
LocationProvider::AccuracyLevel() const
{
	fLock.Lock();
	int32 level = fAccuracyLevel;
	fLock.Unlock();
	return level;
}


void
LocationProvider::_NotifyStatus()
{
	fLock.Lock();
	int32 status = fEnabled
		? (fConnected
			? (fFix >= 3 ? B_LOCATION_3D_FIX
				: fFix == 2 ? B_LOCATION_2D_FIX
				: B_LOCATION_NO_FIX)
			: B_LOCATION_CONNECTING)
		: B_LOCATION_OFFLINE;

	BMessage message(B_LOCATION_STATUS);
	message.AddInt32("status", status);
	message.AddString("error", fLastError.String());
	message.AddString("device", fDevicePath.String());
	message.AddString("source", fSourceName.String());
	message.AddInt32("satellites", fSatellitesInUse);
	message.AddInt32("satellites_view", fSatellitesInView);
	message.AddInt32("fix", fFix);
	message.AddBool("enabled", fEnabled);
	message.AddBool("connected", fConnected);

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
	fLock.Lock();
	if (!fHavePosition) {
		fLock.Unlock();
		return;
	}

	double latitude = fLatitude;
	double longitude = fLongitude;
	double accuracy = fAccuracy;
	int32 level = fAccuracyLevel;
	_ApplyAccuracy(&latitude, &longitude, &accuracy, level);

	BMessage update(B_LOCATION_CHANGED);
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
	double* accuracy, int32 level) const
{
	// Snap to coarser cells so the preflet can show "city" or "country"
	// without a second location service. ~1 km for city, ~50 km for rough.
	if (level == B_LOCATION_ACCURACY_EXACT)
		return;

	double cell = level == B_LOCATION_ACCURACY_APPROXIMATE ? 0.01 : 0.5;
	*latitude = (double)((int64)(*latitude / cell)) * cell;
	*longitude = (double)((int64)(*longitude / cell)) * cell;
	if (accuracy != NULL && *accuracy < cell * 111000.0)
		*accuracy = cell * 111000.0;
}


status_t
LocationProvider::_Connect()
{
	BNetworkAddress address;
	status_t status = address.SetTo(fHost.String(), fPort);
	if (status != B_OK) {
		fLock.Lock();
		fLastError = "could not resolve gpsd host";
		fConnected = false;
		fLock.Unlock();
		return B_ERROR;
	}

	if (fSocket.Connect(address, 5 * 1000000LL) != B_OK) {
		fLock.Lock();
		fLastError = "could not connect to gpsd";
		fConnected = false;
		fLock.Unlock();
		return B_ERROR;
	}

	// Prefer JSON: flat objects, no binary framing. Fall back to raw NMEA
	// if gpsfake is replaying a plain log without WATCH support.
	const char* watch = "?WATCH={\"enable\":true,\"json\":true};\n";
	fSocket.Write(watch, strlen(watch));

	fLock.Lock();
	fLastError = "";
	fConnected = true;
	fLock.Unlock();
	return B_OK;
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
	BString device;
	BString source = "gpsd";

	while (!fQuitRequested) {
		ssize_t bytes = fSocket.Read(buffer, sizeof(buffer) - 1);
		if (bytes <= 0) {
			if (fQuitRequested)
				break;
			_PostStatus(false, "gpsd connection lost", 0, 0, 0,
				source.String(), device.String());
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
				_HandleLine(line, &device, &source);

			index = pending.FindFirst('\n');
		}
	}

	_PostStatus(false, NULL, 0, 0, 0, source.String(), device.String());
}


void
LocationProvider::_HandleLine(const BString& line, BString* device,
	BString* source)
{
	// gpsd JSON objects and NMEA sentences both arrive as lines.
	if (line[0] == '{')
		_HandleJson(line, device, source);
	else if (line[0] == '$')
		_HandleNmea(line, device, source);
}


void
LocationProvider::_HandleJson(const BString& json, BString* device,
	BString* source)
{
	BString classString;
	if (!_JsonString(json, "class", &classString))
		return;

	if (classString == "DEVICES") {
		// Optionally extract the first device path from a nested object.
		const char* path = strstr(json.String(), "\"path\"");
		if (path != NULL) {
			BString pathJson(path);
			BString found;
			if (_JsonString(pathJson, "path", &found) && !found.IsEmpty()) {
				*device = found;
				_PostStatus(true, NULL, 0, 0, 0, source->String(),
					device->String());
			}
		}
		return;
	}

	if (classString == "DEVICE") {
		BString found;
		if (_JsonString(json, "path", &found) && !found.IsEmpty()) {
			*device = found;
			_PostStatus(true, NULL, 0, 0, 0, source->String(),
				device->String());
		}
		return;
	}

	if (classString == "TPV") {
		int32 mode = 0;
		_JsonInt(json, "mode", &mode);
		if (mode < 2)
			return;

		double latitude = 0.0;
		double longitude = 0.0;
		double altitude = 0.0;
		double speed = 0.0;
		double heading = 0.0;
		double value;
		if (_JsonDouble(json, "lat", &value))
			latitude = value;
		if (_JsonDouble(json, "lon", &value))
			longitude = value;
		if (_JsonDouble(json, "alt", &value))
			altitude = value;
		if (_JsonDouble(json, "speed", &value))
			speed = value;
		if (_JsonDouble(json, "track", &value))
			heading = value;

		// Prefer epx/epy (1-sigma in metres); fall back to pdop-ish guess.
		double accuracy;
		double epx = 0.0;
		double epy = 0.0;
		bool hasEpx = _JsonDouble(json, "epx", &epx);
		bool hasEpy = _JsonDouble(json, "epy", &epy);
		if (hasEpx || hasEpy)
			accuracy = hasEpx && hasEpy ? sqrt(epx * epx + epy * epy) : epx;
		else if (_JsonDouble(json, "hdop", &value))
			accuracy = value * 5.0;	// rough metres, HDOP-only receivers
		else
			accuracy = 10.0;

		*source = "gpsd";
		if (device->IsEmpty())
			*device = "gpsd";

		_PostPosition(latitude, longitude, altitude, accuracy, speed, heading,
			mode >= 3 ? 3 : 2, 0, 0, source->String(), device->String());
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

		_PostStatus(true, NULL, 0, used,
			inView > used ? inView : used, source->String(),
			device->String());
		return;
	}
}


void
LocationProvider::_HandleNmea(const BString& sentence, BString* device,
	BString* source)
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
		double latitude = _NmeaCoordToDegrees(fields.StringAt(1),
			fields.StringAt(2)[0]);
		double longitude = _NmeaCoordToDegrees(fields.StringAt(3),
			fields.StringAt(4)[0]);
		int32 quality = atoi(fields.StringAt(5));
		int32 satellites = atoi(fields.StringAt(6));
		if (quality > 0) {
			double altitude = strtod(fields.StringAt(8), NULL);
			*source = "gpsd-nmea";
			if (device->IsEmpty())
				*device = "nmea";
			_PostPosition(latitude, longitude, altitude, 5.0, 0.0, 0.0,
				altitude > 0.0 ? 3 : 2, satellites, 0, source->String(),
				device->String());
		} else {
			_PostStatus(true, NULL, 0, satellites, 0, source->String(),
				device->String());
		}
		return;
	}

	if (suffix == "RMC" && fields.CountStrings() >= 9) {
		char status = fields.StringAt(1)[0];
		if (status != 'A')
			return;

		double latitude = _NmeaCoordToDegrees(fields.StringAt(2),
			fields.StringAt(3)[0]);
		double longitude = _NmeaCoordToDegrees(fields.StringAt(4),
			fields.StringAt(5)[0]);
		// knots -> m/s
		double speed = strtod(fields.StringAt(6), NULL) * 0.514444;
		double heading = strtod(fields.StringAt(7), NULL);
		*source = "gpsd-nmea";
		if (device->IsEmpty())
			*device = "nmea";
		_PostPosition(latitude, longitude, 0.0, 5.0, speed, heading, 2, 0, 0,
			source->String(), device->String());
		return;
	}

	if (suffix == "GSA") {
		// Fields: talker, mode, fix, then 12 sat IDs, PDOP, HDOP, VDOP.
		int32 fix = atoi(fields.StringAt(1));
		if (fix >= 2) {
			double accuracy = 0.0;
			if (fields.CountStrings() >= 16) {
				double hdop = strtod(fields.StringAt(14), NULL);
				double vdop = strtod(fields.StringAt(15), NULL);
				accuracy = sqrt(hdop * hdop + vdop * vdop) * 5.0;
			}
			_PostStatus(true, NULL, fix, 0, 0, source->String(),
				device->String(), accuracy);
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
		if (messageNumber == 1 && satsInView > 0) {
			_PostStatus(true, NULL, 0, 0, satsInView, source->String(),
				device->String());
		}

		// Count used satellites is not in GSV; leave InUse from GGA/GSA.
		if (totalMessages > 0 && messageNumber == totalMessages) {
			_PostStatus(true, NULL, 0, 0, satsInView, source->String(),
				device->String());
		}
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
LocationProvider::_PostPosition(double latitude, double longitude,
	double altitude, double accuracy, double speed, double heading, int32 fix,
	int32 satellites, int32 satellitesView, const char* source,
	const char* device)
{
	BMessage message(kMsgData);
	message.AddDouble("latitude", latitude);
	message.AddDouble("longitude", longitude);
	message.AddDouble("altitude", altitude);
	message.AddDouble("accuracy", accuracy);
	message.AddDouble("speed", speed);
	message.AddDouble("heading", heading);
	message.AddInt64("timestamp", real_time_clock() * 1000000LL);
	message.AddInt32("fix", fix);
	if (satellites > 0)
		message.AddInt32("satellites", satellites);
	if (satellitesView > 0)
		message.AddInt32("satellites_view", satellitesView);
	if (source != NULL)
		message.AddString("source", source);
	if (device != NULL && device[0] != '\0')
		message.AddString("device", device);
	PostMessage(&message);
}


void
LocationProvider::_PostStatus(bool connected, const char* error, int32 fix,
	int32 satellites, int32 satellitesView, const char* source,
	const char* device, double accuracy)
{
	BMessage message(kMsgStatus);
	message.AddBool("connected", connected);
	if (error != NULL)
		message.AddString("error", error);
	if (fix > 0)
		message.AddInt32("fix", fix);
	if (satellites > 0)
		message.AddInt32("satellites", satellites);
	if (satellitesView > 0)
		message.AddInt32("satellites_view", satellitesView);
	if (source != NULL && source[0] != '\0')
		message.AddString("source", source);
	if (device != NULL && device[0] != '\0')
		message.AddString("device", device);
	if (accuracy > 0.0)
		message.AddDouble("accuracy", accuracy);
	PostMessage(&message);
}
