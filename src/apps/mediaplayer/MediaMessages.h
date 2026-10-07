/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA_MESSAGES_H
#define _MEDIA_MESSAGES_H


// Transport and playlist commands, posted to the main window.
enum {
	MSG_OPEN				= 'open',
	MSG_OPEN_LOCATION		= 'opnl',
	MSG_PLAYLIST_SHOW		= 'plsh',
	MSG_PLAYLIST_ADD		= 'plad',
	MSG_PLAYLIST_CLEAR		= 'plcl',
	MSG_PLAY_CD				= 'pcd ',
	MSG_OPEN_MIDI			= 'omdi',
	MSG_QUIT				= 'quit',

	MSG_PLAYLIST_ITEM_INVOKED = 'pliv',
	MSG_PLAYLIST_ITEMS_CHANGED = 'plch',
	MSG_PLAYLIST_CURRENT_CHANGED = 'plcc',

	MSG_CONTROLLER_POSITION	= 'cpoz',
	MSG_CONTROLLER_DURATION	= 'cdur',
	MSG_CONTROLLER_STATE	= 'csts',
	MSG_CONTROLLER_EOS		= 'ceos',
	MSG_CONTROLLER_ERROR	= 'cerr',
	MSG_CONTROLLER_FINISHED	= 'cfin',
	MSG_CONTROLLER_VIDEO	= 'cvid',
	MSG_CONTROLLER_ITEM		= 'citm'
};


#endif // _MEDIA_MESSAGES_H
