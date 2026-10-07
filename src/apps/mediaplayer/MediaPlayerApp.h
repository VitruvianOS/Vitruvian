/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA_PLAYER_APP_H
#define _MEDIA_PLAYER_APP_H


#include <Application.h>
#include <Entry.h>
#include <Message.h>


class MainWin;


class MediaPlayerApp : public BApplication {
public:
								MediaPlayerApp();
	virtual						~MediaPlayerApp();

	virtual	void				ArgvReceived(int32 argc, char** argv);
	virtual	void				RefsReceived(BMessage* message);
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				ReadyToRun();

private:
			MainWin*			fMainWin;
};


#endif // _MEDIA_PLAYER_APP_H
