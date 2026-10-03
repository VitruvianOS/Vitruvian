/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef PROFILE_NAME_WINDOW_H
#define PROFILE_NAME_WINDOW_H


#include <Messenger.h>
#include <Window.h>


class BButton;
class BTextControl;


// Small modal prompt for a profile name. On Save it sends `message` with the
// name added as "profile_name" to `target`.
class ProfileNameWindow : public BWindow {
public:
								ProfileNameWindow(BWindow* parent,
									const char* suggestion,
									const BMessenger& target,
									uint32 messageWhat);

	virtual	void				MessageReceived(BMessage* message);

private:
			void				_UpdateSaveButton();

			BMessenger			fTarget;
			uint32				fWhat;
			BTextControl*		fNameControl;
			BButton*			fSaveButton;
};


#endif // PROFILE_NAME_WINDOW_H
