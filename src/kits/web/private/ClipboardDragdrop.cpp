/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "clipboard_dragdrop.h"

#include "logging.h"

#include <Clipboard.h>

#include <wpe/webkit.h>


//	#pragma mark - Constructor/Destructor


ClipboardDragDrop::ClipboardDragDrop()
{
}


ClipboardDragDrop::~ClipboardDragDrop()
{
}


//	#pragma mark - Public


void
ClipboardDragDrop::Attach(WebKitWebView* view)
{
	// Full clipboard integration in the GObject API runs through user-content
	// scripts injecting clipboard events; for v1.0 we expose the BClipboard
	// bridge via GetClipboardText/SetClipboardText for the wrapper to call
	// in response to Cmd-C / Cmd-V intercepts.
	(void)view;
	log_info("ClipboardDragDrop: attached (BClipboard bridge ready)");
}


std::string
ClipboardDragDrop::GetClipboardText() const
{
	std::string result;

	if (be_clipboard->Lock()) {
		BMessage* data = be_clipboard->Data();
		const void* text = nullptr;
		ssize_t len = 0;
		if (data->FindData("text/plain", B_MIME_TYPE, &text, &len) == B_OK
			&& text != nullptr && len > 0) {
			result.assign(static_cast<const char*>(text), len);
		}
		be_clipboard->Unlock();
	}

	return result;
}


void
ClipboardDragDrop::SetClipboardText(const std::string& text)
{
	if (be_clipboard->Lock()) {
		be_clipboard->Clear();
		BMessage* data = be_clipboard->Data();
		if (data != nullptr) {
			data->AddData("text/plain", B_MIME_TYPE,
				text.c_str(), (ssize_t)text.size());
		}
		be_clipboard->Commit();
		be_clipboard->Unlock();
		log_info("ClipboardDragDrop: set clipboard (%zu bytes)", text.size());
	}
}
