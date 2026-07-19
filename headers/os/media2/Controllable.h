/*
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_CONTROLLABLE_H
#define _MEDIA2_CONTROLLABLE_H


#include <Messenger.h>
#include <ObjectList.h>

#include <media2/MediaDefs.h>


#define B_MEDIA_NEW_PARAMETER_VALUE 'mnpv'


class BParameterWeb;
class BView;


class BControllable {
public:
	typedef void (*change_hook)(int32 id, bigtime_t when,
								const void* value, size_t size, void* cookie);

									BControllable();
	virtual							~BControllable();

			void					SetChangeHook(change_hook hook, void* cookie);

			BParameterWeb*			Web() const;
			status_t				SetParameterWeb(BParameterWeb* web);
				// Takes ownership of `web`.

	virtual	status_t				GetParameterValue(int32 id,
										bigtime_t* lastChangeTime,
										void* value, size_t* ioSize);
	virtual	status_t				SetParameterValue(int32 id,
										bigtime_t changeTime,
										const void* value, size_t size);

			status_t				StartControlPanel(BMessenger watcher);
			status_t				StopControlPanel(BMessenger watcher);
			void					BroadcastChangedParameter(int32 id);

	virtual	BView*					MakeView();
				// Default returns NULL — full BMediaTheme support arrives in
				// Batch G when Media pref needs UI generation.

private:
			struct ValueEntry;
			BParameterWeb*			fWeb;
			BObjectList<ValueEntry, true>		fValues;
			BObjectList<BMessenger, true>		fWatchers;
			change_hook				fChangeHook;
			void*					fChangeHookCookie;
};


#endif // _MEDIA2_CONTROLLABLE_H
