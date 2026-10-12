/*
 * Copyright 2001-2015, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Rafael Romo
 *		Stefano Ceccherini (burton666@libero.it)
 *		Axel Dörfler, axeld@pinc-software.de
 *		Augustin Cavalier <waddlesplash>
 */

#ifndef ALERT_WINDOW_H
#define ALERT_WINDOW_H


#include <Alert.h>
#include <Font.h>
#include <Messenger.h>
#include <String.h>

class BWindow;


class AlertWindow : public BAlert {
	public:
		AlertWindow(BMessenger handler);

		virtual void MessageReceived(BMessage* message);
		virtual void DispatchMessage(BMessage* message, BHandler* handler);

		// A new change joined the pending window; give it a full
		// countdown again.
		void ResetCountdown();

		// Close without messaging the handler: the caller reverts the
		// change itself.
		void Dismiss();

	private:
		void UpdateCountdownView();
		void Resolve(uint32 what);

		int32			fSeconds;
		bool			fResolved;
		BMessenger		fHandler;
		BFont			fOriginalFont;
		BFont			fFont;
};

#endif	/* ALERT_WINDOW_H */
