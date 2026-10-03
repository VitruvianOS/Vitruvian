/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * gpsd TCP source: JSON WATCH on a configured host, NMEA fallback.
 */

#include "GpsdSource.h"
#include "GpsdParser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <Message.h>
#include <NetworkAddress.h>
#include <StringList.h>


GpsdSource::GpsdSource(const char* host, uint16 port)
	:
	fHost(host != NULL ? host : "127.0.0.1"),
	fPort(port),
	fThread(-1),
	fRunning(false),
	fQuitRequested(false)
{
}


GpsdSource::~GpsdSource()
{
	Stop();
}


status_t
GpsdSource::Start(BMessenger target)
{
	if (!target.IsValid())
		return B_BAD_VALUE;

	fLock.Lock();
	if (fRunning && fThread >= 0) {
		fLock.Unlock();
		return B_OK;
	}
	fQuitRequested = false;
	fLock.Unlock();

	Stop();

	fLock.Lock();
	fTarget = target;
	fQuitRequested = false;
	fLock.Unlock();

	if (_Connect() != B_OK)
		return B_ERROR;

	fLock.Lock();
	fThread = spawn_thread(_ThreadEntry, "gpsd reader",
		B_NORMAL_PRIORITY, this);
	if (fThread < 0) {
		fRunning = false;
		fLock.Unlock();
		if (fSocket.InitCheck() == B_OK)
			fSocket.Disconnect();
		return B_ERROR;
	}
	thread_id thread = fThread;
	fRunning = true;
	fLock.Unlock();
	resume_thread(thread);
	return B_OK;
}


void
GpsdSource::Stop()
{
	fLock.Lock();
	fQuitRequested = true;
	fRunning = false;
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
}


status_t
GpsdSource::_Connect()
{
	BNetworkAddress address;
	status_t status = address.SetTo(fHost.String(), fPort);
	if (status != B_OK)
		return B_ERROR;

	if (fSocket.Connect(address, 5 * 1000000LL) != B_OK)
		return B_ERROR;

	// Prefer JSON: flat objects, no binary framing. Fall back to raw NMEA
	// if gpsfake is replaying a plain log without WATCH support.
	const char* watch = "?WATCH={\"enable\":true,\"json\":true};\n";
	fSocket.Write(watch, strlen(watch));
	return B_OK;
}


int32
GpsdSource::_ThreadEntry(void* data)
{
	GpsdSource* source = (GpsdSource*)data;
	source->_ReadLoop();
	return 0;
}


void
GpsdSource::_ReadLoop()
{
	char buffer[4096];
	BString pending;
	BString device;
	BString sourceName = "gpsd";

	while (!fQuitRequested) {
		ssize_t bytes = fSocket.Read(buffer, sizeof(buffer) - 1);
		if (bytes <= 0) {
			if (fQuitRequested)
				break;

			BMessage status(kMsgLocationStatus);
			status.AddBool("connected", false);
			status.AddString("error", "gpsd connection lost");
			status.AddString("source", sourceName.String());
			if (!device.IsEmpty())
				status.AddString("device", device.String());
			fLock.Lock();
			fTarget.SendMessage(&status);
			fLock.Unlock();
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

			if (!line.IsEmpty()) {
				GpsdFix fix;
				GpsdSourceStatus update;
				if (GpsdParseLine(line, &fix, &update, &device)) {
					fLock.Lock();
					if (fix.valid) {
						BMessage data(kMsgLocationData);
						data.AddDouble("latitude", fix.latitude);
						data.AddDouble("longitude", fix.longitude);
						data.AddDouble("altitude", fix.altitude);
						data.AddDouble("accuracy", fix.accuracy);
						data.AddDouble("speed", fix.speed);
						data.AddDouble("heading", fix.heading);
						data.AddInt64("timestamp",
							real_time_clock() * 1000000LL);
						data.AddInt32("fix", fix.fix);
						if (fix.satellitesUsed > 0)
							data.AddInt32("satellites", fix.satellitesUsed);
						if (fix.satellitesInView > 0)
							data.AddInt32("satellites_view",
								fix.satellitesInView);
						if (!fix.source.IsEmpty())
							data.AddString("source", fix.source.String());
						if (!fix.device.IsEmpty())
							data.AddString("device", fix.device.String());
						sourceName = fix.source;
						if (!fix.device.IsEmpty())
							device = fix.device;
						fTarget.SendMessage(&data);
					}
					if (update.connected || !update.error.IsEmpty()
						|| update.satellitesUsed > 0
						|| update.satellitesInView > 0 || update.fix > 0) {
						BMessage status(kMsgLocationStatus);
						status.AddBool("connected", true);
						if (!update.error.IsEmpty())
							status.AddString("error", update.error.String());
						if (update.fix > 0)
							status.AddInt32("fix", update.fix);
						if (update.satellitesUsed > 0)
							status.AddInt32("satellites",
								update.satellitesUsed);
						if (update.satellitesInView > 0)
							status.AddInt32("satellites_view",
								update.satellitesInView);
						if (!update.source.IsEmpty())
							status.AddString("source", update.source.String());
						if (!update.device.IsEmpty())
							status.AddString("device", update.device.String());
						if (update.accuracy > 0.0)
							status.AddDouble("accuracy", update.accuracy);
						if (!update.source.IsEmpty())
							sourceName = update.source;
						if (!update.device.IsEmpty())
							device = update.device;
						fTarget.SendMessage(&status);
					}
				}
				fLock.Unlock();
			}

			index = pending.FindFirst('\n');
		}
	}

	fLock.Lock();
	BMessage status(kMsgLocationStatus);
	status.AddBool("connected", false);
	status.AddString("source", sourceName.String());
	if (!device.IsEmpty())
		status.AddString("device", device.String());
	fTarget.SendMessage(&status);
	fLock.Unlock();
}
