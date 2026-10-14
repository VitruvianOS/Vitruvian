/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _IN_CALL_WINDOW_H
#define _IN_CALL_WINDOW_H


#include <Window.h>
#include <String.h>

#include <Bitmap.h>
#include <Locker.h>
#include <View.h>


class BButton;
class BStringView;


// Self/far-end video surface for SIP video calls: the roster's frame
// hook drops I420 planes here, converted to RGB32 for drawing.
class CallVideoView : public BView {
public:
								CallVideoView(const BRect& frame);

			void				SetFrame(const uint8* const* data,
									const int* stride, int width,
									int height);

	virtual	void				Draw();
	virtual	void				MessageReceived(BMessage* message);

private:
			BLocker				fLock;
			BBitmap*			fBitmap;
};


// In-call UI: status, keypad for DTMF, mute/speaker/hold and hang up.
// Call audio switches to the UCM "Voice Call" verb for as long as this
// window lives.
class InCallWindow : public BWindow {
public:
								InCallWindow(const char* number);
	virtual						~InCallWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	void				Pulse();

private:
			void				_AppendDtmf(const char* digit);
			void				_ToggleMute();
			void				_ToggleSpeaker();
			void				_Hold();
			void				_HangUp();
			void				_Update();

			BString				fNumber;
			BStringView*		fStatusLabel;
			BStringView*		fNumberLabel;
			BButton*			fMuteButton;
			BButton*			fSpeakerButton;
			CallVideoView*		fVideoView;
			bool				fMuted;
			bool				fLoudspeaker;
};


#endif // _IN_CALL_WINDOW_H
