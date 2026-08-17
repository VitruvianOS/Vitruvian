/*
 * Copyright 2015-2026, Dario Casalinuovo. All rights reserved.
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_SIMPLE_MEDIA_UNIT_H
#define _MEDIA2_SIMPLE_MEDIA_UNIT_H


#include <media2/MediaUnit.h>


class BSimpleMediaUnit : public BMediaUnit {
public:
	enum notification {
		B_WILL_START = 1,
		B_WILL_STOP,
		B_WILL_SEEK,
		B_FORMAT_SUGGESTION
	};

	typedef void (*notify_hook)(void* cookie, notification what, ...);
	// A unit owns one filter driving every port at once, so the hook is
	// handed the whole cycle and pulls what it needs with BufferFor(),
	// unlike BSimpleMediaNode's per-connection process_hook.
	typedef void (*cycle_hook)(void* cookie, BSimpleMediaUnit* unit,
		uint32 frameCount);
	typedef status_t (*format_hook)(void* cookie, BMediaFormat* format);

									BSimpleMediaUnit(const char* name,
										media_client_kinds kinds
											= B_MEDIA_PLAYER);
	virtual							~BSimpleMediaUnit();

			void					SetNotifyHook(notify_hook hook, void* cookie);
			void					SetCycleHook(cycle_hook hook, void* cookie);
			void					SetFormatHook(format_hook hook, void* cookie);

			using BMediaUnit::BufferFor;

protected:
	virtual	void					HandleStart(bigtime_t performanceTime) override;
	virtual	void					HandleStop(bigtime_t performanceTime) override;
	virtual	void					HandleSeek(bigtime_t mediaTime,
										bigtime_t performanceTime) override;
	virtual	void					ProcessCycle(uint32 frameCount) override;

private:
			notify_hook				fNotifyHook;
			cycle_hook				fCycleHook;
			format_hook				fFormatHook;
			void*					fNotifyCookie;
			void*					fCycleCookie;
			void*					fFormatCookie;
};


#endif
