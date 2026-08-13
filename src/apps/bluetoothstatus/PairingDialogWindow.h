/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef PAIRING_DIALOG_WINDOW_H
#define PAIRING_DIALOG_WINDOW_H


#include <Messenger.h>
#include <String.h>
#include <Window.h>


class BButton;
class BStringView;
class BTextControl;


// One kind per Agent1 callback. RequestPinCode/DisplayPinCode and
// RequestAuthorization/AuthorizeService share a table row in the design doc
// because they differ only in wording, not shape -- kept as separate kinds
// here so each renders its own exact copy.
enum pairing_dialog_kind {
	kPairingRequestConfirmation = 0,
	kPairingRequestPasskey,
	kPairingDisplayPasskey,
	kPairingRequestPinCode,
	kPairingDisplayPinCode,
	kPairingRequestAuthorization,
	kPairingAuthorizeService
};


// Fields the caller fills in fRequest before Show(): "device_name" (always),
// "passkey" (uint32, Request/DisplayPasskey and confirmation), "service_name"
// (AuthorizeService only). Nothing here talks to BlueZ -- the live Agent1
// integration fills the request from the real call and reads the result
// back out.
class PairingDialogWindow : public BWindow {
public:
								PairingDialogWindow(pairing_dialog_kind kind,
									const BMessage& request,
									const BMessenger& replyTarget,
									uint32 replyWhat);
	virtual						~PairingDialogWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_BuildConfirmation(const BString& device,
									uint32 passkey);
			void				_BuildPasskeyEntry(const BString& device);
			void				_BuildPasskeyDisplay(const BString& device,
									uint32 passkey);
			void				_BuildPinEntry(const BString& device);
			void				_BuildPinDisplay(const BString& device,
									const BString& pin);
			void				_BuildAuthorization(const BString& device,
									const BString& service, bool isService);
			void				_SendResult(bool accepted,
									const BString& value);

			pairing_dialog_kind	fKind;
			BMessenger			fReplyTarget;
			uint32				fReplyWhat;
			bool				fResultSent;
			BTextControl*		fEntry;

			// 0 unless opened from a live Agent1 call; echoed back on the
			// result so the caller can complete the right pending D-Bus
			// invocation.
			uint32				fRequestId;
};


#endif	// PAIRING_DIALOG_WINDOW_H
