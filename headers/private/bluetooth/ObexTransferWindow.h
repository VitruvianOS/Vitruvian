/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Transfer window: file name, progress, Cancel, and a finish report.
 */
#ifndef _PRIVATE_BLUETOOTH_OBEX_TRANSFER_WINDOW_H
#define _PRIVATE_BLUETOOTH_OBEX_TRANSFER_WINDOW_H


#include <Message.h>
#include <Messenger.h>
#include <String.h>
#include <Window.h>


class BButton;
class BSlider;
class BStringView;


class ObexTransferWindow : public BWindow {
public:
	// files must contain "refs". deviceName is shown in the title; the
	// address is what ObexClient::SendFiles talks to.
								ObexTransferWindow(const char* deviceName,
									const char* targetAddress,
									const BMessage& files);
	virtual						~ObexTransferWindow();

	virtual	void				MessageReceived(BMessage* message);

private:
			void				_StartSend();
			void				_ApplyProgress(BMessage* message);

			BString				fDeviceName;
			BString				fTargetAddress;
			BMessage			fFiles;
			BString				fCurrentFile;

			BStringView*		fFileView;
			BSlider*			fProgress;
			BStringView*		fStatusView;
			BButton*			fCancelButton;
};


#endif // _PRIVATE_BLUETOOTH_OBEX_TRANSFER_WINDOW_H
