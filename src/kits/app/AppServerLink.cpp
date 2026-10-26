/*
 * Copyright 2001-2005, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Erik Jaesler (erik@cgsoftware.com)
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include <Application.h>

#include <ApplicationPrivate.h>
#include <AppServerLink.h>

#include <Locker.h>


/**	AppServerLink provides proxied access to the application's
 *	connection with the app_server.
 *	It has BAutolock semantics:
 *	creating one locks the app_server connection; destroying one
 *	unlocks the connection.
 */


static BLocker sLock("AppServerLink_sLock");


namespace BPrivate {

AppServerLink::AppServerLink()
{
	sLock.Lock();

	// if there is no be_app, we can't do a whole lot, anyway
	if (be_app != NULL) {
		fReceiver = &BApplication::Private::ServerLink()->Receiver();
		fSender = &BApplication::Private::ServerLink()->Sender();
	} else {
		debugger("You need to have a valid app_server connection first!");
	}
}


AppServerLink::~AppServerLink()
{
	sLock.Unlock();
}

}	// namespace BPrivate
