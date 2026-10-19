/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#ifndef _CAMERA_APP_H
#define _CAMERA_APP_H


#include <Application.h>
#include <Entry.h>
#include <Message.h>
#include <Path.h>
#include <String.h>


class CameraWindow;


class CameraApp : public BApplication {
public:
								CameraApp();

	virtual	void				ReadyToRun();
	virtual	void				RefsReceived(BMessage* message);

			BMessage*			Settings() { return &fSettings; }
			status_t			SaveSettings();

			// Ensures a short click WAV exists for play_sound(); returns
			// its entry ref.
			status_t			ShutterSoundRef(entry_ref* outRef);

private:
			BMessage			fSettings;
			BPath				fSettingsPath;
};


#endif // _CAMERA_APP_H
