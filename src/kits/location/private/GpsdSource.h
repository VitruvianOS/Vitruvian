/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * gpsd TCP source: JSON WATCH on a configured host, NMEA fallback.
 */
#ifndef GPSD_SOURCE_H
#define GPSD_SOURCE_H

#include <Locker.h>
#include <OS.h>
#include <Socket.h>
#include <String.h>

#include "LocationSource.h"


class GpsdSource : public LocationSource {
public:
							GpsdSource(const char* host, uint16 port);
	virtual					~GpsdSource();

	virtual	status_t		Start(BMessenger target);
	virtual	void			Stop();
	virtual	const char*		Name() const { return "gpsd"; }

			// Applies to the next connection; call while stopped.
			void			SetEndpoint(const char* host, uint16 port);

private:
			status_t		_Connect();
			void			_ReadLoop();

	static	int32			_ThreadEntry(void* data);

private:
			mutable BLocker	fLock;

			BString			fHost;
			uint16			fPort;
			BSocket			fSocket;
			thread_id		fThread;
			bool			fRunning;
			volatile bool	fQuitRequested;
			BMessenger		fTarget;
};


#endif	// GPSD_SOURCE_H
