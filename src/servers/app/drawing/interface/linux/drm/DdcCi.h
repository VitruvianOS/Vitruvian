/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef DDC_CI_H
#define DDC_CI_H


#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>


// Monitor brightness (VCP 0x10) over DDC/CI on the connector's i2c bus.
// Probing and writing run on a worker thread; callers never block.

class DdcCi {
public:
								DdcCi();
								~DdcCi();

	// connector is the sysfs name, e.g. "card1-HDMI-A-4". Probes async.
			void				Start(const char* connector);
			void				Stop();

			bool				Supported() const;
			int					Max() const;
			// Last known value; -1 when unknown.
			int					Current() const;
			// Only the latest requested value is sent.
			void				Request(int value);
			std::string			Bus() const;
			// "idle", "probing", "ok", "no-ddc", "no-bus", "no-reply".
			std::string			State() const;

private:
			void				_Run(std::string connector, int);

			mutable std::mutex	fLock;
			std::condition_variable	fWake;
			std::thread			fThread;
			bool				fQuit;
			bool				fSupported;
			int					fMax;
			int					fCurrent;
			int					fPending;
			std::string			fBus;
			std::string			fState;
};


#endif	// DDC_CI_H
