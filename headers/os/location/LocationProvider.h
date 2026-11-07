/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * BeAPI location provider for V\OS; apps and preflets watch it for fixes.
 */
#ifndef LOCATION_PROVIDER_H
#define LOCATION_PROVIDER_H

#include <Locker.h>
#include <List.h>
#include <Looper.h>
#include <String.h>


// Delivered to watchers. Payload fields:
//   "latitude" double   "longitude" double   "altitude" double
//   "accuracy" double   (metres)
//   "speed" double      (m/s)   "heading" double  (degrees)
//   "timestamp" int64   (microseconds, real_time_clock)
//   "satellites" int32  (used)  "satellites_view" int32
//   "fix" int32         (0 none, 2 2D, 3 3D)
//   "source" string     "device" string
#define B_LOCATION_CHANGED 'BLOC'

// Source connection state. Fields:
//   "status" int32  (see location_status)
//   "error" string  "device" string  "source" string
//   "satellites" int32  "satellites_view" int32  "fix" int32
#define B_LOCATION_STATUS  'BLST'


enum location_status {
	B_LOCATION_OFFLINE = 0,
	B_LOCATION_CONNECTING,
	B_LOCATION_NO_FIX,
	B_LOCATION_2D_FIX,
	B_LOCATION_3D_FIX,
};


// Coarse levels the preflet exposes. Exact is a straight pass-through;
// Approximate snaps to a city-scale cell; Rough snaps to a country-scale
// cell. Applied in LocationProvider::_Publish().
enum location_accuracy {
	B_LOCATION_ACCURACY_EXACT = 0,
	B_LOCATION_ACCURACY_APPROXIMATE = 1,
	B_LOCATION_ACCURACY_ROUGH = 2,
};


class LocationSource;


class LocationProvider : public BLooper {
public:
							LocationProvider(const char* host = "127.0.0.1",
								uint16 port = 2947);
	virtual					~LocationProvider();

	virtual	void			MessageReceived(BMessage* message);
	virtual	void			Quit();

			status_t		Start();
			void			Stop();
			bool			IsEnabled() const;

			status_t		Watch(BMessenger target);
			status_t		Watch(BHandler* handler);
			status_t		Unwatch(BMessenger target);
			status_t		Unwatch(BHandler* handler);

			bool			IsConnected() const;
			int32			FixType() const;
			int32			SatellitesInView() const;
			int32			SatellitesInUse() const;
			BString			DevicePath() const;
			BString			SourceName() const;
			BString			LastError() const;

			void			LastLocation(double* latitude, double* longitude,
								double* altitude, double* accuracy,
								double* speed, double* heading,
								int64* timestamp) const;

			void			SetAccuracyLevel(int32 level);
			int32			AccuracyLevel() const;

			// Reconnects to a different gpsd endpoint when enabled.
			void			SetEndpoint(const char* host, uint16 port);

			const char*		Host() const { return fHost.String(); }
			uint16			Port() const { return fPort; }

private:
			void			_NotifyStatus();
			void			_Publish();
			void			_ApplyAccuracy(double* latitude,
								double* longitude, double* accuracy,
								int32 level) const;

private:
			mutable BLocker	fLock;
			BList			fWatchers;

			BString			fHost;
			uint16			fPort;

			bool			fEnabled;
			bool			fConnected;

			BString			fSourceName;
			BString			fDevicePath;
			BString			fLastError;
			int32			fAccuracyLevel;
			int32			fFix;
			int32			fSatellitesInView;
			int32			fSatellitesInUse;

			double			fLatitude;
			double			fLongitude;
			double			fAltitude;
			double			fAccuracy;
			double			fSpeed;
			double			fHeading;
			int64			fTimestamp;

			bool			fHavePosition;

			LocationSource*	fSource;
};


#endif	// LOCATION_PROVIDER_H
