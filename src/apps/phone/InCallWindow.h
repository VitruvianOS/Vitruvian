/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _IN_CALL_WINDOW_H
#define _IN_CALL_WINDOW_H


#include <Window.h>
#include <String.h>


class BButton;
class BStringView;


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
			bool				fMuted;
			bool				fLoudspeaker;
};


#endif // _IN_CALL_WINDOW_H
