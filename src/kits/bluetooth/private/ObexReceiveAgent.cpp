/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <ObexReceiveAgent.h>
#include <ObexReceivePrompt.h>
#include <ObexClient.h>

#include <BluetoothSettings.h>

#include <Catalog.h>
#include <Application.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <Entry.h>
#include <Notification.h>
#include <Path.h>
#include <Roster.h>
#include <String.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Bluetooth receive"


ObexReceiveAgent::ObexReceiveAgent(BluetoothSettings& settings)
	:
	BHandler("obex_receive_agent"),
	fSettings(settings),
	fRunning(false)
{
}


ObexReceiveAgent::~ObexReceiveAgent()
{
	Stop();
}


status_t
ObexReceiveAgent::Start()
{
	if (fRunning)
		return B_OK;

	status_t status = ObexClient::RegisterAgent(BMessenger(this),
		BMessenger(be_app), ObexClient::kReply);
	if (status != B_OK)
		return status;

	fRunning = true;
	return B_OK;
}


void
ObexReceiveAgent::Stop()
{
	if (!fRunning)
		return;
	ObexClient::UnregisterAgent(BMessenger(be_app), ObexClient::kReply);
	fRunning = false;
}


void
ObexReceiveAgent::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case ObexClient::kAgentRequest:
			_HandleAgentRequest(message);
			break;

		case ObexClient::kAgentCancel:
			// Pending prompt is moot; the backend already completed the call.
			break;

		case ObexClient::kReceiveComplete:
			_HandleReceiveComplete(message);
			break;

		case ObexClient::kReply:
			break;

		default:
			BHandler::MessageReceived(message);
			break;
	}
}


void
ObexReceiveAgent::_HandleAgentRequest(BMessage* message)
{
	// Reload on every request: the preflet can flip the switch or
	// always-accept list while this agent is running.
	fSettings.LoadSettings();

	uint32 requestId = 0;
	message->FindUInt32("request_id", &requestId);

	// Reject when receiving is off. The agent stays registered in the
	// Deskbar replicant for the whole login; the switch only gates pushes.
	if (!fSettings.ReceiveFiles()) {
		ObexClient::CompleteRequest(requestId, false);
		return;
	}

	BString sender;
	message->FindString("sender", &sender);

	BString fileName;
	message->FindString("file_name", &fileName);

	uint64 fileSize = 0;
	message->FindUInt64("file_size", &fileSize);

	// Auto-accept only when the user ticked "always accept from this device".
	if (!sender.IsEmpty() && fSettings.AlwaysAccept(sender)) {
		ObexClient::CompleteRequest(requestId, true);
		return;
	}

	bool canAlways = !sender.IsEmpty();
	ObexReceivePrompt* prompt = new ObexReceivePrompt(requestId, sender,
		fileName, fileSize, canAlways, &fSettings, sender.String());
	prompt->Show();
}


void
ObexReceiveAgent::_HandleReceiveComplete(BMessage* message)
{
	BString filePath;
	message->FindString("file_path", &filePath);

	BString fileName;
	message->FindString("file_name", &fileName);

	_NotifyReceived(filePath, fileName);
}


void
ObexReceiveAgent::_NotifyReceived(const BString& filePath,
	const BString& fileName)
{
	BString name = fileName;
	if (name.IsEmpty() && !filePath.IsEmpty()) {
		BEntry entry(filePath.String());
		BPath path;
		if (entry.GetPath(&path) == B_OK)
			name = path.Leaf();
	}

	// Ensure ~/Downloads exists. obexd stores incoming OPP files there by
	// default; a missing directory makes "received" look like a failure.
	BPath userPath;
	if (find_directory(B_USER_DIRECTORY, &userPath, true) == B_OK) {
		BPath downloads(userPath.Path(), "Downloads");
		create_directory(downloads.Path(), 0755);
	}

	BNotification notification(B_INFORMATION_NOTIFICATION);
	notification.SetGroup(B_TRANSLATE("Bluetooth"));
	notification.SetTitle(B_TRANSLATE("File received"));
	BString content;
	content << B_TRANSLATE("Received ") << name
		<< B_TRANSLATE(" from a Bluetooth device");
	notification.SetContent(content);

	if (!filePath.IsEmpty()) {
		BEntry entry(filePath.String());
		entry_ref ref;
		if (entry.GetRef(&ref) == B_OK) {
			// Open the file's folder on click.
			BEntry parent(filePath.String(), true);
			entry_ref folderRef;
			if (parent.GetParent(&parent) == B_OK
					&& parent.GetRef(&folderRef) == B_OK) {
				notification.AddOnClickRef(&folderRef);
			} else {
				notification.AddOnClickRef(&ref);
			}
		}
	}
	notification.Send();
}
