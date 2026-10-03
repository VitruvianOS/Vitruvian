/*
 * Copyright 2003-2014 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Jérôme Duval, jerome.duval@free.fr
 * 		Michael Phipps
 *		John Scipione, jscipione@gmail.com
 *		Puck Meerburg, puck@puckipedia.nl
 *		Dario Casalinuovo
 */
#ifndef SCREEN_SAVER_APP_H
#define SCREEN_SAVER_APP_H


#include "ScreenSaverSettings.h"
#include "ScreenSaverRunner.h"
#include "ScreenSaverWindow.h"

#include <Application.h>
#include <MessageRunner.h>


const static uint32 kMsgResumeSaver = 'RSSV';


class ScreenBlanker : public BApplication {
public:
								ScreenBlanker();
								~ScreenBlanker();

	virtual	void				ReadyToRun();

	virtual	bool				QuitRequested();
	virtual	void				ArgvReceived(int argc, char** argv);
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_RequestLock();
			void				_QueueResumeScreenSaver();
			void				_TurnOnScreen();
			void				_SetDPMSMode(uint32 mode);
			void				_QueueTurnOffScreen();
			void				_Shutdown();

			ScreenSaverSettings	fSettings;
			ScreenSaverWindow*	fWindow;
			ScreenSaverRunner*	fSaverRunner;

			bigtime_t			fBlankTime;
			bool				fImmediateLock;
			bool				fTestSaver;
			BMessageRunner*		fResumeRunner;

			BMessageRunner*		fStandByScreenRunner;
			BMessageRunner*		fSuspendScreenRunner;
			BMessageRunner*		fTurnOffScreenRunner;
};

#endif	// SCREEN_SAVER_APP_H
