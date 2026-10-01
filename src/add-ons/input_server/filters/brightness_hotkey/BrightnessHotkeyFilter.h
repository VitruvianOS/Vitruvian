/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _BRIGHTNESS_HOTKEY_INPUT_FILTER_H
#define _BRIGHTNESS_HOTKEY_INPUT_FILTER_H


#include <InputServerFilter.h>


extern "C" _EXPORT BInputServerFilter* instantiate_input_filter();


class BrightnessHotkeyFilter : public BInputServerFilter {
public:
								BrightnessHotkeyFilter();

	virtual	filter_result		Filter(BMessage* message, BList* _list);
	virtual	status_t			InitCheck();
};


#endif // _BRIGHTNESS_HOTKEY_INPUT_FILTER_H
