/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */
#ifndef _SUGGEST_POPUP_H
#define _SUGGEST_POPUP_H


#include <OS.h>
#include <Rect.h>
#include <Window.h>

#include <string>
#include <vector>


class BMessenger;
class SuggestPopupView;


enum {
	kSuggestUpdate = 'sgup'
		// "urls" string array plus "selection" int32
};


/*!	Autocomplete dropdown under the URL bar: a borderless floating
	window that never takes focus, so the address field keeps the
	keystrokes while the list tracks the typing. Accepts send
	MSG_SUGGEST_ACCEPTED with "url" back to the owner view.
*/
class SuggestPopup : public BWindow {
public:
								SuggestPopup(const BMessenger& owner);
	virtual						~SuggestPopup();

	virtual bool				QuitRequested();
	virtual void				MessageReceived(BMessage* message);

			// where: screen point the list opens under, clamped to
			// the screen the way BMenu::_CalcFrame does.
			void				Go(BPoint where);

			void				Update(const std::vector<std::string>& urls,
									int32_t selection);

private:
	friend class SuggestPopupView;

			SuggestPopupView*	fView;
			BMessenger*			fOwner;
};


#endif // _SUGGEST_POPUP_H
