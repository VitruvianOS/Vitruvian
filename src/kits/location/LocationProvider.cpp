/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * BeAPI location provider for V\OS; apps and preflets watch it for fixes.
 */

#include <LocationProvider.h>

#include "GpsdSource.h"
#include "LocationSource.h"

#include <Message.h>
#include <Messenger.h>
#include <OS.h>


static const uint32 kMsgStart = 'LcSt';
static const uint32 kMsgStop = 'LcSp';
static const uint32 kMsgSetEndpoint = 'LcEp';


LocationProvider::LocationProvider(const char* host, uint16 port)
	:
	BLooper("LocationProvider"),
	fHost(host != NULL ? host : "127.0.0.1"),
	fPort(port),
	fEnabled(false),
	fConnected(false),
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
	fHavePosition(false),
	fSource(new GpsdSource(fHost.String(), fPort))
{
}


LocationProvider::~LocationProvider()
{
	Stop();
	delete fSource;
}


void
LocationProvider::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgStart:
		{
			fLock.Lock();
			if (fEnabled && fConnected) {
				fLock.Unlock();
				break;
			}
			fLock.Unlock();

			// Reconnect path: join any previous source reader first.
			Stop();

			if (fSource == NULL
				|| fSource->Start(BMessenger(this)) != B_OK) {
				fLock.Lock();
				fLastError = "could not start location source";
				fConnected = false;
				fSourceName = fSource != NULL ? fSource->Name() : "location";
				fLock.Unlock();
				_NotifyStatus();
				break;
			}

			fLock.Lock();
			fEnabled = true;
			fConnected = true;
			fSourceName = fSource->Name();
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
		case kMsgSetEndpoint:
		{
			const char* host = NULL;
			message->FindString("host", &host);
			int32 port = 0;
			message->FindInt32("port", &port);

			fLock.Lock();
			bool restart = fEnabled;
			if (host != NULL && host[0] != '\0')
				fHost = host;
			if (port != 0)
				fPort = (uint16)port;
			fConnected = false;
			fLock.Unlock();

			// Join the reader before re-pointing it, then reconnect on the
			// same looper thread so Start() cannot race SetEndpoint().
			Stop();
			if (fSource != NULL)
				fSource->SetEndpoint(fHost.String(), fPort);

			if (restart)
				Start();
			else
				_NotifyStatus();
			break;
		}
		case kMsgLocationData:
		{
			// Source posted a parsed position snapshot; apply it on the
			// looper so watchers and getters see one owner.
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
		case kMsgLocationStatus:
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
	if (fEnabled && fConnected) {
		fLock.Unlock();
		return B_OK;
	}
	fLock.Unlock();
	return PostMessage(kMsgStart);
}


void
LocationProvider::SetEndpoint(const char* host, uint16 port)
{
	BMessage message(kMsgSetEndpoint);
	if (host != NULL)
		message.AddString("host", host);
	message.AddInt32("port", port);
	PostMessage(&message);
}


void
LocationProvider::Stop()
{
	fLock.Lock();
	fEnabled = false;
	fConnected = false;
	fLock.Unlock();

	if (fSource != NULL)
		fSource->Stop();
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


BString
LocationProvider::DevicePath() const
{
	// By value: the looper may reassign the string once the lock drops.
	fLock.Lock();
	BString path = fDevicePath;
	fLock.Unlock();
	return path;
}


BString
LocationProvider::SourceName() const
{
	// By value: the looper may reassign the string once the lock drops.
	fLock.Lock();
	BString source = fSourceName;
	fLock.Unlock();
	return source;
}


BString
LocationProvider::LastError() const
{
	// By value: the looper may reassign the string once the lock drops.
	fLock.Lock();
	BString error = fLastError;
	fLock.Unlock();
	return error;
}


void
LocationProvider::LastLocation(double* latitude, double* longitude,
	double* altitude, double* accuracy, double* speed, double* heading,
	int64* timestamp) const
{
	fLock.Lock();
	// Readers see the same coarsened view watchers get, otherwise the
	// accuracy setting looks like it does nothing in the preflet.
	double snappedLatitude = fLatitude;
	double snappedLongitude = fLongitude;
	double snappedAccuracy = fAccuracy;
	_ApplyAccuracy(&snappedLatitude, &snappedLongitude, &snappedAccuracy,
		fAccuracyLevel);

	if (latitude != NULL)
		*latitude = snappedLatitude;
	if (longitude != NULL)
		*longitude = snappedLongitude;
	if (altitude != NULL)
		*altitude = fAltitude;
	if (accuracy != NULL)
		*accuracy = snappedAccuracy;
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
