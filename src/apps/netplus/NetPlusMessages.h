/*
 * Copyright 2026, VitruvianOS Project.
 * Distributed under the terms of the LGPL License.
 */
#ifndef NET_PLUS_MESSAGES_H
#define NET_PLUS_MESSAGES_H


#include <SupportDefs.h>


enum {
	MSG_NAV_BACK		= 'navb',
	MSG_NAV_FORWARD		= 'navf',
	MSG_NAV_RELOAD		= 'navr',
	MSG_NAV_STOP		= 'navs',
	MSG_NAV_HOME		= 'navh',
	MSG_OPEN_URL		= 'ourl',
	// The settings window saved: re-apply preferences to the view.
	MSG_SETTINGS_CHANGED = 'setc',
	// Open a window; "url" string, "private" bool (optional).
	MSG_OPEN_WINDOW		= 'nowi',
	// Bookmarks menu items; "url" string.
	MSG_BOOKMARK_GO		= 'bmgo',
	MSG_BOOKMARK_ADD	= 'bmad',
	MSG_BOOKMARK_REMOVE	= 'bmrm',
	// Rebuild the Bookmarks menu after add/remove.
	MSG_BOOKMARKS_CHANGED = 'bmcg',
	MSG_SHOW_DOWNLOADS	= 'dlsr',
	MSG_SHOW_SETTINGS	= 'stsw'
};


#endif // NET_PLUS_MESSAGES_H
