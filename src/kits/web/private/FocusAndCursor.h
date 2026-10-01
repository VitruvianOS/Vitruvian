/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef VOS_FOCUS_AND_CURSOR_H
#define VOS_FOCUS_AND_CURSOR_H


#include <wpe/wpe.h>


class FocusAndCursorAdapter {
public:
						FocusAndCursorAdapter();
						~FocusAndCursorAdapter();

			void		SetBackend(struct wpe_view_backend* backend);
			void		SetFocused(bool focused);

private:
	struct wpe_view_backend* fBackend;
};


#endif // VOS_FOCUS_AND_CURSOR_H
