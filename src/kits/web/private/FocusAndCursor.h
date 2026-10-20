/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef VOS_FOCUS_AND_CURSOR_H
#define VOS_FOCUS_AND_CURSOR_H


#ifdef HAS_WPE_PLATFORM
#include <wpe/wpe-platform.h>
#else
#include <wpe/wpe.h>
#endif


class FocusAndCursorAdapter {
public:
						FocusAndCursorAdapter();
						~FocusAndCursorAdapter();

#ifdef HAS_WPE_PLATFORM
			void		SetView(WPEView* view);
#else
			void		SetBackend(struct wpe_view_backend* backend);
#endif
			void		SetFocused(bool focused);

private:
#ifdef HAS_WPE_PLATFORM
	WPEView*			fView;
#else
	struct wpe_view_backend* fBackend;
#endif
};


#endif // VOS_FOCUS_AND_CURSOR_H
