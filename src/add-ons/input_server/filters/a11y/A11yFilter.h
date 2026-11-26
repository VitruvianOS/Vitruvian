/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * input_server filter for V\OS accessibility input aids.
 */
#ifndef A11Y_FILTER_H
#define A11Y_FILTER_H


#include <InputServerFilter.h>
#include <Keymap.h>
#include <List.h>
#include <Message.h>

#include "AccessibilitySettings.h"


extern "C" _EXPORT BInputServerFilter* instantiate_input_filter();


class A11yFilter : public BInputServerFilter {
public:
								A11yFilter();

	virtual	filter_result		Filter(BMessage* message, BList* list);
	virtual	status_t			InitCheck();

private:
			void				_ReloadSettings(bool force);
			bool				_HandleGlobalShortcut(int32 key,
									uint32 modifiers);
			filter_result		_HandleStickyTap(int32 key);
			filter_result		_ApplyStickyModifiers(BMessage* message,
									BList* list);
			filter_result		_HandleMouseKeys(int32 key, uint32 modifiers,
									BMessage* message, BList* list);
			bool				_SwallowMouseKeyUp(int32 key, uint32 modifiers);
			filter_result		_SlowKeysOnKeyDown(BMessage* message,
									int32 key);
			filter_result		_SlowKeysOnKeyUp(BMessage* message, int32 key,
									BList* list);
			filter_result		_SlowKeysOnOther(BMessage* message,
									BList* list);
			void				_FlushPendingSlowKey(BList* list);

			BKeymap				fKeymap;
			accessibility_settings	fSettings;
			bigtime_t			fSettingsTime;

			int32				fStickyTapCount;
			bigtime_t			fLastStickyTap;

			// Slow keys: one stashed keydown awaiting the hold-time check.
			bool				fHasPendingKey;
			BMessage			fPendingKey;
			int32				fPendingKeyKey;
			bigtime_t			fPendingKeySince;

			// Sticky keys: one-shot latch plus a persistent lock.
			uint32				fLatchedModifiers;
			uint32				fLockedModifiers;

			// Sticky mods applied to a keydown, replayed on its key-up.
			int32				fStickyAppliedKey;
			uint32				fStickyAppliedMods;
};


#endif	// A11Y_FILTER_H
