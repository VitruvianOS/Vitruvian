/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Private OBEX file-transfer facade. Keeps BlueZBackend.h out of consumers.
 */
#ifndef _PRIVATE_BLUETOOTH_OBEX_CLIENT_H
#define _PRIVATE_BLUETOOTH_OBEX_CLIENT_H


#include <Messenger.h>
#include <Message.h>
#include <String.h>


class ObexClient {
public:
	enum {
		// Agent1 (incoming push) UI handler
		kAgentRequest = 'BTOR',
		kAgentCancel = 'BTON',
		// Outgoing send queue uiHandler
		kTransferStarted = 'BTOS',
		kTransferProgress = 'BTOP',
		kTransferComplete = 'BTOK',
		kTransferFailed = 'BTXF',
		kQueueComplete = 'BTQC',
		// Incoming receive finished
		kReceiveComplete = 'BTRC',
		// Async call replyTo messages carry "status"
		kReply = 'BTOY'
	};

	// Registers the obex Agent1 with uiHandler as the accept/reject UI and
	// replies "status" to replyTo; safe to call from a window thread.
	static status_t RegisterAgent(const BMessenger& uiHandler,
		const BMessenger& replyTo, uint32 replyWhat);
	static status_t UnregisterAgent(const BMessenger& replyTo,
		uint32 replyWhat);

	// Completes a kAgentRequest. accepted=false rejects the push.
	static void CompleteRequest(uint32 requestId, bool accepted);

	// Sends every ref in files ("refs") to targetAddress in one session.
	// uiHandler receives the transfer BMessages above.
	static status_t SendFiles(const char* targetAddress, const BMessage& files,
		const BMessenger& uiHandler, const BMessenger& replyTo,
		uint32 replyWhat);
	static void CancelSend();
};


#endif // _PRIVATE_BLUETOOTH_OBEX_CLIENT_H
