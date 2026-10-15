/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */
#ifndef _LOCAL_SEND_APP_H
#define _LOCAL_SEND_APP_H

#include <Application.h>
#include <Archivable.h>
#include <Message.h>
#include <View.h>

class LocalSendManager;
class LocalSendSettings;
class LocalSendWindow;

// Message the tray sends to the running app to toggle the window.
static const uint32 kMsgToggleWindow = 'LStg';

// Tray replicant shown in the Deskbar. It is loaded into the
// Deskbar's address space, so it talks to the app by signature
// through a BMessenger, never through a direct pointer.
class LocalSendReplicant : public BView {
public:
						LocalSendReplicant(BRect frame);

	virtual	void		AttachedToWindow();
	virtual	void		MouseDown(BPoint where);
	virtual	void		Draw(BRect updateRect);

	virtual	status_t	Archive(BMessage* into, bool deep = true) const;
	static	BArchivable*	Instantiate(BMessage* into);

private:
			void		_ToggleAppWindow();
};


class LocalSendApp : public BApplication {
public:
						LocalSendApp();
	virtual				~LocalSendApp();

	virtual	void		ReadyToRun();
	virtual	void		ArgvReceived(int32 argc, char** argv);
	virtual	void		MessageReceived(BMessage* message);
	virtual	void		RefsReceived(BMessage* message);
	virtual	bool		QuitRequested();

private:
			void		_ToggleWindow();
			void		_OpenFilesFromRefs(BMessage* message);

			LocalSendSettings*	fSettings;
			LocalSendManager*	fManager;
			LocalSendWindow*	fWindow;
			bool		fQuitImmediately;
};

#endif // _LOCAL_SEND_APP_H
