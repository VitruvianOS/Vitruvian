/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <ObexTransferWindow.h>
#include <ObexClient.h>

#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <Notification.h>
#include <Slider.h>
#include <StringView.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Bluetooth transfer"


static const uint32 kMsgCancel = 'otcl';


ObexTransferWindow::ObexTransferWindow(const char* deviceName,
	const char* targetAddress, const BMessage& files)
	:
	BWindow(BRect(80, 80, 420, 200), B_TRANSLATE("Bluetooth transfer"),
		B_TITLED_WINDOW, B_NOT_RESIZABLE | B_NOT_ZOOMABLE
			| B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fDeviceName(deviceName != NULL ? deviceName : ""),
	fTargetAddress(targetAddress != NULL ? targetAddress : ""),
	fFiles(files)
{
	fFileView = new BStringView("file", "");
	fFileView->SetTruncation(B_TRUNCATE_MIDDLE);

	fProgress = new BSlider("progress", "", NULL, 0, 100, B_HORIZONTAL);
	fProgress->SetEnabled(false);
	fProgress->SetValue(0);

	fStatusView = new BStringView("status", B_TRANSLATE("Starting" B_UTF8_ELLIPSIS));

	fCancelButton = new BButton("cancel", B_TRANSLATE("Cancel"),
		new BMessage(kMsgCancel));

	BString title(B_TRANSLATE("Send to %device%"));
	title.ReplaceFirst("%device%", fDeviceName);
	SetTitle(title.String());

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(fFileView)
		.Add(fProgress)
		.Add(fStatusView)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.AddGlue()
			.Add(fCancelButton)
		.End()
	.End();

	CenterOnScreen();

	// Defer the send until the window is running: SendFiles() posts back
	// to this looper, which must already be pumping.
	PostMessage(new BMessage('osts'));
}


ObexTransferWindow::~ObexTransferWindow()
{
}


void
ObexTransferWindow::_StartSend()
{
	status_t status = ObexClient::SendFiles(fTargetAddress.String(), fFiles,
		BMessenger(this), BMessenger(this), ObexClient::kReply);
	if (status != B_OK) {
		BString text(B_TRANSLATE("Couldn't start the Bluetooth transfer: "
			"%error%"));
		text.ReplaceFirst("%error%", strerror(status));
		BAlert* alert = new BAlert(B_TRANSLATE("Bluetooth transfer"), text,
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->Go();
		PostMessage(B_QUIT_REQUESTED);
	}
}


void
ObexTransferWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case 'osts':
			fCancelButton->SetTarget(this);
			_StartSend();
			break;

		case kMsgCancel:
			ObexClient::CancelSend();
			fStatusView->SetText(B_TRANSLATE("Cancelling" B_UTF8_ELLIPSIS));
			break;

		case ObexClient::kTransferStarted:
		{
			const char* name = NULL;
			if (message->FindString("file_name", &name) == B_OK) {
				fCurrentFile = name;
				fFileView->SetText(name);
			}
			int32 count = 0;
			int32 index = 0;
			message->FindInt32("count", &count);
			message->FindInt32("index", &index);
			if (count > 0) {
				BString status;
				status << B_TRANSLATE("Sending file ") << (index + 1)
					<< B_TRANSLATE(" of ") << count;
				fStatusView->SetText(status.String());
			}
			break;
		}

		case ObexClient::kTransferProgress:
			_ApplyProgress(message);
			break;

		case ObexClient::kTransferComplete:
		{
			BString status;
			status << B_TRANSLATE("Sent ") << (fCurrentFile.IsEmpty()
				? B_TRANSLATE("file") : fCurrentFile.String());
			fStatusView->SetText(status.String());
			break;
		}

		case ObexClient::kTransferFailed:
		{
			const char* error = NULL;
			message->FindString("error", &error);
			BString text = error != NULL ? error
				: B_TRANSLATE("The transfer failed.");
			BAlert* alert = new BAlert(B_TRANSLATE("Bluetooth transfer"),
				text, B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
				B_STOP_ALERT);
			alert->Go();
			PostMessage(B_QUIT_REQUESTED);
			break;
		}

		case ObexClient::kQueueComplete:
		{
			int32 status = B_OK;
			message->FindInt32("status", &status);
			if (status == B_OK) {
				BNotification notification(B_INFORMATION_NOTIFICATION);
				notification.SetGroup(B_TRANSLATE("Bluetooth"));
				notification.SetTitle(B_TRANSLATE("Bluetooth transfer"));
				BString content;
				content << B_TRANSLATE("Sent files to ") << fDeviceName;
				notification.SetContent(content);
				notification.Send();
				fStatusView->SetText(B_TRANSLATE("Transfer complete"));
			} else if (status != B_CANCELED) {
				fStatusView->SetText(B_TRANSLATE("Transfer failed"));
			} else {
				fStatusView->SetText(B_TRANSLATE("Transfer cancelled"));
			}
			fCancelButton->SetLabel(B_TRANSLATE("Close"));
			fCancelButton->SetMessage(new BMessage(B_QUIT_REQUESTED));
			break;
		}

		case ObexClient::kReply:
		{
			// Reply from SendFiles() itself; real progress arrives as the
			// uiHandler messages above. Ignore.
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
ObexTransferWindow::_ApplyProgress(BMessage* message)
{
	uint64 transferred = 0;
	uint64 size = 0;
	message->FindUInt64("transferred", &transferred);
	message->FindUInt64("size", &size);

	if (size > 0)
		fProgress->SetValue((int32)((transferred * 100) / size));
	else
		fProgress->SetValue(0);

	const char* status = NULL;
	if (message->FindString("status", &status) == B_OK && status != NULL)
		fStatusView->SetText(status);

	const char* name = NULL;
	if (message->FindString("file_name", &name) == B_OK && name != NULL
			&& name[0] != '\0') {
		fCurrentFile = name;
		fFileView->SetText(name);
	}
}
