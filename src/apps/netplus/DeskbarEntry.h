/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */
#ifndef _DESKBAR_ENTRY_H
#define _DESKBAR_ENTRY_H


#include <Path.h>
#include <SupportDefs.h>


/*!	The Deskbar Applications menu reads its menu_entries folder: a
	directory of symlinks, one per entry, each launched by refs. An
	installed PWA is exactly that: a symlink named after the app,
	pointing at the NetPlus binary, whose own node carries the
	manifest icon (BNode on a link ref does not traverse it).
*/
class DeskbarEntry {
public:
	static	status_t	EntryDirectory(BPath* into, bool create);

			// <user settings>/NetPlus/webapps: where the store keeps
			// its records and cached icons.
	static	status_t	WebAppStorePath(BPath* into);

			// Links name to targetPath and, when iconPath is given
			// (a PNG), stamps it onto the link node.
	static	status_t	Create(const char* name, const char* targetPath,
							const char* iconPath);
	static	status_t	Remove(const char* name);
};


#endif // _DESKBAR_ENTRY_H
