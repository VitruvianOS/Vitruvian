/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <Application.h>
#include <Message.h>
#include <Window.h>

#include "BluetoothWindow.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Bluetooth"


// Existing signature; do not change.
static const char* kSignature = "application/x-vnd.Haiku-Bluetooth";


class Application : public BApplication {
public:
								Application();

public:
	virtual	void				ReadyToRun();
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_OpenInquiry();

			BluetoothWindow*	fWindow;
			bool				fPendingOpenInquiry;
};


Application::Application()
	:
	BApplication(kSignature),
	fWindow(NULL),
	fPendingOpenInquiry(false)
{
}


void
Application::ReadyToRun()
{
	fWindow = new BluetoothWindow();
	fWindow->Show();

	if (fPendingOpenInquiry) {
		fPendingOpenInquiry = false;
		fWindow->PostMessage(kMsgOpenInquiry);
	}
}


void
Application::MessageReceived(BMessage* message)
{
	if (message->what == kMsgOpenInquiry) {
		_OpenInquiry();
		return;
	}

	BApplication::MessageReceived(message);
}


void
Application::_OpenInquiry()
{
	// On a cold start this arrives before ReadyToRun creates the window.
	if (fWindow == NULL) {
		fPendingOpenInquiry = true;
		return;
	}

	fWindow->PostMessage(kMsgOpenInquiry);
}


int
main(int /*argc*/, char** /*argv*/)
{
	Application* app = new Application();
	app->Run();
	delete app;
	return 0;
}
