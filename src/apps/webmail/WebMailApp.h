/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef WEB_MAIL_APP_H
#define WEB_MAIL_APP_H


#include <Application.h>
#include <String.h>


class MainWindow;


// application/x-vnd.vos-webmail: one window, the account list and the
// mail views. Everything account-shaped goes through AccountsService.
class WebMailApp : public BApplication {
public:
								WebMailApp();
	virtual						~WebMailApp();

	virtual void				ReadyToRun();
	virtual void				RefsReceived(BMessage* message);
	virtual void				ArgvReceived(int32 argc, char** argv);

			void				OpenWindow();
			void				WindowClosed(MainWindow* window);

	// Directory holding the bundled vos-mail: UI, next to the
	// executable or in the system apps directory.
	static	const char*			UiDirectory();

private:
			void				_HandleMailto(const char* url);

	MainWindow*					fWindow;
};


#endif // WEB_MAIL_APP_H
