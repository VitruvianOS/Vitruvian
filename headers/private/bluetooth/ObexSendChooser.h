/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Device picker plus transfer-window launcher for outgoing OBEX sends.
 */
#ifndef _PRIVATE_BLUETOOTH_OBEX_SEND_CHOOSER_H
#define _PRIVATE_BLUETOOTH_OBEX_SEND_CHOOSER_H


#include <Handler.h>
#include <Message.h>
#include <Messenger.h>


class ObexSendChooser : public BHandler {
public:
	// files must contain "refs". Fetches paired devices, shows a picker,
	// then opens ObexTransferWindow for the choice.
	static	void				Send(const BMessage& files);

								ObexSendChooser(const BMessage& files);
	virtual						~ObexSendChooser();

	virtual	void				MessageReceived(BMessage* message);

private:
			void				_ShowPicker(BMessage* devicesReply);

			BMessage			fFiles;
};


#endif // _PRIVATE_BLUETOOTH_OBEX_SEND_CHOOSER_H
