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
	MSG_CONTROLLER_ITEM		= 'citm',

	// BeOS-shaped UX: fullscreen, loop, info, view options.
	MSG_TOGGLE_FULLSCREEN	= 'tfsc',
	MSG_FULLSCREEN_TICK		= 'fftk',
	MSG_FULLSCREEN_HIDE		= 'ffhd',
	MSG_REVEAL_CONTROLS		= 'rvcl',
	MSG_TOGGLE_LOOP			= 'tlop',
	MSG_FILE_INFO			= 'finf',
	MSG_ALWAYS_ON_TOP		= 'atop',
	MSG_HIDE_INTERFACE		= 'hint',
	MSG_VIDEO_SCALE			= 'vscl',
	MSG_VIDEO_FIT			= 'vfit',
	MSG_ZOOM_IN				= 'zmin',
	MSG_ZOOM_OUT			= 'zmou',
	MSG_CONTEXT_MENU		= 'ctxm',
	MSG_NEW_PLAYER			= 'npla'
};


#endif // _MEDIA_MESSAGES_H
