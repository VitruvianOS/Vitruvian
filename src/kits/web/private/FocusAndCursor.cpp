/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "focus_and_cursor.h"

#include "logging.h"


FocusAndCursorAdapter::FocusAndCursorAdapter()
	:
	fBackend(nullptr)
{
}


FocusAndCursorAdapter::~FocusAndCursorAdapter()
{
}


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
