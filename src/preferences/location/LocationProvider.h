/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Client-side location provider for V\OS. Speaks the gpsd JSON/NMEA
 * protocol on localhost (or a configured host) and delivers updates to
 * registered BMessenger/BHandler watchers as B_LOCATION_CHANGED.
 *
 * Prototype: the gpsd backend is the only source wired up. ModemManager
 * and geoclue2 are planned layers on top; this class is the BeAPI
 * surface those layers will feed.
 */
#ifndef LOCATION_PROVIDER_H
#define LOCATION_PROVIDER_H

#include <Locker.h>
#include <List.h>
#include <Looper.h>
#include <Socket.h>
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
//   "status" int32  (see LocationStatus)
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

			const char*		Host() const { return fHost.String(); }
			uint16			Port() const { return fPort; }

private:
			void			_NotifyStatus();
			void			_Publish();
			void			_ApplyAccuracy(double* latitude,
								double* longitude, double* accuracy,
								int32 level) const;

			status_t		_Connect();
			void			_ReadLoop();
			void			_HandleLine(const BString& line, BString* device,
								BString* source);
			void			_HandleJson(const BString& json, BString* device,
								BString* source);
			void			_HandleNmea(const BString& sentence,
								BString* device, BString* source);
			void			_SplitNmea(const BString& sentence,
								BString* type, BStringList* fields) const;

			void			_PostPosition(double latitude, double longitude,
								double altitude, double accuracy, double speed,
								double heading, int32 fix, int32 satellites,
								int32 satellitesView, const char* source,
								const char* device);
			void			_PostStatus(bool connected, const char* error,
								int32 fix, int32 satellites,
								int32 satellitesView, const char* source,
								const char* device,
								double accuracy = 0.0);

	static	int32			_ThreadEntry(void* data);

private:
			mutable BLocker	fLock;
			BList			fWatchers;

			BString			fHost;
			uint16			fPort;
			BSocket			fSocket;
			thread_id		fThread;
			bool			fEnabled;
			bool			fConnected;
			volatile bool	fQuitRequested;

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
};


#endif	// LOCATION_PROVIDER_H
