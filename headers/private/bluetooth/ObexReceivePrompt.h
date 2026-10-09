/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Accept/Reject prompt for an incoming OBEX push. Never auto-accepts.
 */
#ifndef _PRIVATE_BLUETOOTH_OBEX_RECEIVE_PROMPT_H
#define _PRIVATE_BLUETOOTH_OBEX_RECEIVE_PROMPT_H


#include <Messenger.h>
#include <String.h>
#include <Window.h>


class BCheckBox;
class BButton;
class BStringView;
class BluetoothSettings;


class ObexReceivePrompt : public BWindow {
public:
	// Completes ObexClient request requestId; "always accept" is saved to
	// settings before the request completes.
								ObexReceivePrompt(uint32 requestId,
									const BString& sender,
									const BString& fileName,
									uint64 fileSize,
									bool canAlwaysAccept,
									BluetoothSettings* settings,
									const char* senderAddress);
	virtual						~ObexReceivePrompt();

	virtual	void				MessageReceived(BMessage* message);

private:
			uint32				fRequestId;
			BluetoothSettings*	fSettings;
			BString				fSenderAddress;

			BStringView*		fDetailView;
			BCheckBox*			fAlwaysCheckBox;
			BButton*			fAcceptButton;
			BButton*			fRejectButton;
};


#endif // _PRIVATE_BLUETOOTH_OBEX_RECEIVE_PROMPT_H
