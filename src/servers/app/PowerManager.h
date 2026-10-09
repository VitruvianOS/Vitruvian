/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H


#include <OS.h>


class Desktop;
struct sd_bus;


// Turns the display off after the user's idle time and tells logind whether the session is
// idle, which drives IdleAction. EventDispatcher turns the display back on at the next input.
class PowerManager {
public:
								PowerManager(Desktop* desktop);
								~PowerManager();

			status_t			Start();

private:
	static	status_t			_ThreadEntry(void* self);
			void				_Run();
			void				_LoadSettings();
			void				_SetIdleHint(bool idle);

			Desktop*			fDesktop;
			thread_id			fThread;
			int32				fQuit;
			bigtime_t			fDisplayOff;
			bigtime_t			fSettingsTime;
			bool				fIdleHint;
			sd_bus*				fBus;
};


#endif	// POWER_MANAGER_H
