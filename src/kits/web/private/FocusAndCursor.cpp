/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "focus_and_cursor.h"

#include "logging.h"


FocusAndCursorAdapter::FocusAndCursorAdapter()
	:
#ifdef HAS_WPE_PLATFORM
	fView(NULL)
#else
	fBackend(NULL)
#endif
{
}


FocusAndCursorAdapter::~FocusAndCursorAdapter()
{
}


#ifdef HAS_WPE_PLATFORM
void
FocusAndCursorAdapter::SetView(WPEView* view)
{
	fView = view;
	// Mapping and the initial size are the view's own job (the
	// toplevel attach maps it); focus arrives from the window.
}


void
FocusAndCursorAdapter::SetFocused(bool focused)
{
	if (fView == NULL)
		return;

	if (focused)
		wpe_view_focus_in(fView);
	else
		wpe_view_focus_out(fView);
}
#else
void
FocusAndCursorAdapter::SetBackend(struct wpe_view_backend* backend)
{
	fBackend = backend;

	// Initial state: visible and in window, not yet focused.
	wpe_view_backend_add_activity_state(fBackend,
		wpe_view_activity_state_visible | wpe_view_activity_state_in_window);
}


void
FocusAndCursorAdapter::SetFocused(bool focused)
{
	if (!fBackend)
		return;

	if (focused)
		wpe_view_backend_add_activity_state(fBackend,
			wpe_view_activity_state_focused);
	else
		wpe_view_backend_remove_activity_state(fBackend,
			wpe_view_activity_state_focused);
}
#endif
