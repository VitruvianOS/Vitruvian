/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "InCallWindow.h"

#include <Autolock.h>
#include <Button.h>
#include <GroupView.h>
#include <Catalog.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "InCallWindow"
#include <LayoutBuilder.h>
#include <Locale.h>
#include <Messenger.h>
#include <StringView.h>

#include <stdio.h>

#include "CallAudioRouter.h"
#include "DialerWindow.h"
#include "SipAudioRouter.h"
#include <telephony/Modem.h>
#include <telephony/TelephonyRoster.h>


// #pragma mark - CallVideoView


CallVideoView::CallVideoView(const BRect& frame)
	:
	BView(frame, "video", B_FOLLOW_ALL, B_WILL_DRAW),
	fBitmap(NULL)
{
	SetViewColor(0, 0, 0);
}


void
CallVideoView::SetFrame(const uint8* const* data, const int* stride,
	int width, int height)
{
	BAutolock lock(fLock);

	if (fBitmap == NULL || fBitmap->Bounds().Width() + 1 != width
			|| fBitmap->Bounds().Height() + 1 != height) {
		delete fBitmap;
		fBitmap = new BBitmap(BRect(0, 0, width - 1, height - 1),
			B_RGB32);
	}
	if (fBitmap == NULL || fBitmap->InitCheck() != B_OK)
		return;

	// I420 to RGB32, nearest neighbor; fine for a self view.
	uint8* bits = (uint8*)fBitmap->Bits();
	int32 bpr = fBitmap->BytesPerRow();
	const uint8* yPlane = data[0];
	const uint8* uPlane = data[1];
	const uint8* vPlane = data[2];
	for (int row = 0; row < height; row++) {
		uint8* out = bits + row * bpr;
		for (int col = 0; col < width; col++) {
			int y = yPlane[row * stride[0] + col];
			int u = uPlane[(row / 2) * stride[1] + col / 2] - 128;
			int v = vPlane[(row / 2) * stride[2] + col / 2] - 128;
			int r = y + (int)(1.402 * v);
			int g = y - (int)(0.344 * u) - (int)(0.714 * v);
			int b = y + (int)(1.772 * u);
			out[col * 4 + 0] = (uint8)(b < 0 ? 0 : b > 255 ? 255 : b);
			out[col * 4 + 1] = (uint8)(g < 0 ? 0 : g > 255 ? 255 : g);
			out[col * 4 + 2] = (uint8)(r < 0 ? 0 : r > 255 ? 255 : r);
			out[col * 4 + 3] = 255;
		}
	}
	// BView has no PostMessage of its own; route through the looper.
	if (Looper() != NULL)
		Looper()->PostMessage('cvFM', this);
}


void
CallVideoView::MessageReceived(BMessage* message)
{
	if (message->what == 'cvFM') {
		Invalidate();
		return;
	}
	BView::MessageReceived(message);
}


void
CallVideoView::Draw(BRect /*updateRect*/)
{
	BAutolock lock(fLock);
	if (fBitmap == NULL) {
		SetHighColor(0, 0, 0);
		FillRect(Bounds());
		return;
	}
	DrawBitmap(fBitmap, Bounds());
}


// #pragma mark - Frame hook


static CallVideoView* sVideoView = NULL;


static void
_VideoFrameHook(void* cookie, const uint8* const* data, const int* stride,
	int width, int height, bool local)
{
	CallVideoView* view = (CallVideoView*)cookie;
	if (view != NULL)
		view->SetFrame(data, stride, width, height);
}


static BMessage
_DtmfMessage(const char* digit)
{
	BMessage message(kMsgKeypadKey);
	message.AddString("be:value", digit);
	return message;
}


// Finds the line that owns an active or ringing call; the in-call
// window drives whichever line the call landed on, modem or SIP.
static BModem*
_ActiveLine()
{
	BTelephonyRoster* roster = BTelephonyRoster::Default();
	for (int32 i = 0; i < roster->CountModems(); i++) {
		BModem* modem = roster->ModemAt(i);
		if (modem == NULL)
			continue;
		uint32 state = modem->CallState();
		if (state != B_TELEPHONY_CALL_UNKNOWN
				&& state != B_TELEPHONY_CALL_TERMINATED)
			return modem;
	}
	return roster->ModemAt(0);
}


InCallWindow::InCallWindow(const char* number)
	:
	BWindow(BRect(80, 80, 360, 560), B_TRANSLATE("Call in progress"),
		B_TITLED_WINDOW, B_AVOID_FRONT | B_NOT_RESIZABLE),
	fNumber(number != NULL ? number : ""),
	fMuted(false),
	fLoudspeaker(false)
{
	fNumberLabel = new BStringView("number", fNumber.String());
	fNumberLabel->SetFont(be_bold_font);
	fStatusLabel = new BStringView("status", B_TRANSLATE("Active"));

	fMuteButton = new BButton("mute", B_TRANSLATE("Mute"),
		new BMessage('phMU'));
	fSpeakerButton = new BButton("speaker", B_TRANSLATE("Speaker"),
		new BMessage('phSP'));

	BGroupView* keypad = new BGroupView(B_TRANSLATE("Keypad"),
		B_HORIZONTAL, 4);
	BLayoutBuilder::Group<>(keypad, B_VERTICAL, 4)
		.AddGrid(4, 4)
			.Add(new BButton("k1", "1", new BMessage(_DtmfMessage("1"))),
				0, 0)
			.Add(new BButton("k2", "2", new BMessage(_DtmfMessage("2"))),
				1, 0)
			.Add(new BButton("k3", "3", new BMessage(_DtmfMessage("3"))),
				2, 0)
			.Add(new BButton("k4", "4", new BMessage(_DtmfMessage("4"))),
				0, 1)
			.Add(new BButton("k5", "5", new BMessage(_DtmfMessage("5"))),
				1, 1)
			.Add(new BButton("k6", "6", new BMessage(_DtmfMessage("6"))),
				2, 1)
			.Add(new BButton("k7", "7", new BMessage(_DtmfMessage("7"))),
				0, 2)
			.Add(new BButton("k8", "8", new BMessage(_DtmfMessage("8"))),
				1, 2)
			.Add(new BButton("k9", "9", new BMessage(_DtmfMessage("9"))),
				2, 2)
			.Add(new BButton("kstar", "*",
				new BMessage(_DtmfMessage("*"))), 0, 3)
			.Add(new BButton("k0", "0", new BMessage(_DtmfMessage("0"))),
				1, 3)
			.Add(new BButton("khash", "#",
				new BMessage(_DtmfMessage("#"))), 2, 3)
		.End();

	BButton* holdButton = new BButton("hold", B_TRANSLATE("Hold"),
		new BMessage('phHD'));
	BButton* hangUpButton = new BButton("hangup", B_TRANSLATE("Hang up"),
		new BMessage('phHU'));
	hangUpButton->SetExplicitMinSize(BSize(B_SIZE_UNSET, 48));

	fVideoView = new CallVideoView(BRect(0, 0, 160, 120));
	fVideoView->SetExplicitSize(BSize(160, 120));
	sVideoView = fVideoView;
	BTelephonyRoster::Default()->SetVideoFrameHook(_VideoFrameHook,
		fVideoView);

	// SIP video calls bring their own camera pipeline; the roster
	// answers B_UNSUPPORTED for modem lines and no-camera hosts.
	BTelephonyRoster::Default()->StartVideo("/dev/video0");

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_SMALL_INSETS)
		.Add(fNumberLabel)
		.Add(fStatusLabel)
		.Add(fVideoView)
		.Add(keypad)
		.AddGrid()
			.Add(fMuteButton, 0, 0)
			.Add(fSpeakerButton, 1, 0)
			.Add(holdButton, 0, 1, 2, 1)
		.End()
		.Add(hangUpButton);

	CallAudioRouter::Default()->BeginCall();
	// SIP lines carry audio as RTP; the router pipes G.711 PCM
	// between the backend and the media kit's PipeWire streams.
	SipAudioRouter::Default()->BeginCall();
}


InCallWindow::~InCallWindow()
{
	SipAudioRouter::Default()->EndCall();
	BTelephonyRoster::Default()->SetVideoFrameHook(NULL, NULL);
	BTelephonyRoster::Default()->StopVideo();
	sVideoView = NULL;
	CallAudioRouter::Default()->EndCall();
}


void
InCallWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgKeypadKey:
		{
			const char* digit = NULL;
			if (message->FindString("be:value", &digit) == B_OK)
				_AppendDtmf(digit);
			break;
		}
		case 'phMU':
			_ToggleMute();
			break;
		case 'phSP':
			_ToggleSpeaker();
			break;
		case 'phHD':
			_Hold();
			break;
		case 'phHU':
			_HangUp();
			break;
		case kTelephonyMsgCallStateChanged:
		case kTelephonyMsgCallRemoved:
			_Update();
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
InCallWindow::Pulse()
{
	_Update();
}


void
InCallWindow::_AppendDtmf(const char* digit)
{
	BModem* modem = _ActiveLine();
	if (modem == NULL || digit == NULL)
		return;

	modem->SendDtmf(digit, BMessenger(this), kMsgOpReply);
}


void
InCallWindow::_ToggleMute()
{
	fMuted = !fMuted;
	CallAudioRouter::Default()->SetMuted(fMuted);
	fMuteButton->SetLabel(fMuted ? B_TRANSLATE("Unmute")
		: B_TRANSLATE("Mute"));
}


void
InCallWindow::_ToggleSpeaker()
{
	fLoudspeaker = !fLoudspeaker;
	CallAudioRouter::Default()->SetRoute(fLoudspeaker
		? CallAudioRouter::ROUTE_SPEAKER : CallAudioRouter::ROUTE_HANDSET);
	fSpeakerButton->SetLabel(fLoudspeaker ? B_TRANSLATE("Earpiece")
		: B_TRANSLATE("Speaker"));
}


void
InCallWindow::_Hold()
{
	BModem* modem = _ActiveLine();
	if (modem != NULL)
		modem->Hold(BMessenger(this), kMsgOpReply);
}


void
InCallWindow::_HangUp()
{
	BModem* modem = _ActiveLine();
	if (modem != NULL)
		modem->HangUp(BMessenger(this), kMsgOpReply);
	fStatusLabel->SetText(B_TRANSLATE("Hanging up..."));
	PostMessage(B_QUIT_REQUESTED);
}


void
InCallWindow::_Update()
{
	BModem* modem = _ActiveLine();
	if (modem == NULL)
		return;

	uint32 state = modem->CallState();
	if (state == B_TELEPHONY_CALL_UNKNOWN
			|| state == B_TELEPHONY_CALL_TERMINATED) {
		PostMessage(B_QUIT_REQUESTED);
		return;
	}

	fStatusLabel->SetText(modem->CallStateName());
	const char* number = modem->CallNumber();
	if (number != NULL && fNumber != number) {
		fNumber = number;
		fNumberLabel->SetText(fNumber.String());
	}
}
