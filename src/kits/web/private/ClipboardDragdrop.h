/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef VOS_CLIPBOARD_DRAGDROP_H
#define VOS_CLIPBOARD_DRAGDROP_H


#include <string>
#include <wpe/webkit.h>


class ClipboardDragDrop {
public:
						ClipboardDragDrop();
						~ClipboardDragDrop();

			void		Attach(WebKitWebView* view);

			std::string	GetClipboardText() const;
			void		SetClipboardText(const std::string& text);
};


#endif // VOS_CLIPBOARD_DRAGDROP_H
