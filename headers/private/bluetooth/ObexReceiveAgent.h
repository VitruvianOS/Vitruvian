/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Long-lived receive host: registers the obex Agent1 and routes pushes.
 */
#ifndef _PRIVATE_BLUETOOTH_OBEX_RECEIVE_AGENT_H
#define _PRIVATE_BLUETOOTH_OBEX_RECEIVE_AGENT_H


#include <Handler.h>
#include <Message.h>
#include <Messenger.h>
#include <String.h>


class BluetoothSettings;


class ObexReceiveAgent : public BHandler {
public:
	// settings is borrowed and must outlive this handler.
								ObexReceiveAgent(BluetoothSettings& settings);
	virtual						~ObexReceiveAgent();

	virtual	void				MessageReceived(BMessage* message);

			status_t			Start();
			void				Stop();

private:
			void				_HandleAgentRequest(BMessage* message);
			void				_HandleReceiveComplete(BMessage* message);
			void				_NotifyReceived(const BString& filePath,
									const BString& fileName);

			BluetoothSettings&	fSettings;
			bool				fRunning;
};


#endif // _PRIVATE_BLUETOOTH_OBEX_RECEIVE_AGENT_H
