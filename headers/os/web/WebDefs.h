/*
 * Copyright 2026, VitruvianOS Project.
 * Distributed under the terms of the LGPL License.
 */
#ifndef WEB_DEFS_H
#define WEB_DEFS_H


#include <SupportDefs.h>


// Web page notification message what-codes
enum {
	B_WEB_PAGE_TITLE_CHANGED	= 'WhTt',
	B_WEB_PAGE_URL_CHANGED		= 'WhUc',
	B_WEB_PAGE_PROGRESS_CHANGED	= 'WhPr',
	B_WEB_PAGE_LOAD_FINISHED	= 'WhLf',
	B_WEB_PAGE_LOAD_FAILED		= 'WhFl',
	B_WEB_FRAME_READY			= 'WhFr',
	// Link (and image) under the pointer changed; "" when the pointer
	// left the link. Carries B_WEB_URL_FIELD, B_WEB_IMAGE_FIELD.
	B_WEB_PAGE_HOVER_CHANGED	= 'WhHv',
	// Right-click: show the context menu. Carries B_WEB_URL_FIELD (link
	// under pointer, may be empty), B_WEB_IMAGE_FIELD (image under
	// pointer, may be empty).
	B_WEB_PAGE_CONTEXT_MENU		= 'WhCx',
	// Cursor shape over page content changed; B_WEB_CURSOR_FIELD holds
	// a B_CURSOR_ID_* value.
	B_WEB_PAGE_CURSOR_CHANGED	= 'WhCu',
	// Web process died; the view shows an error page with Reload.
	B_WEB_PAGE_PROCESS_CRASHED	= 'WhCr',
	// Full-screen video: window should enter (bool in
	// B_WEB_FULLSCREEN_FIELD) or the view confirmed the change.
	B_WEB_PAGE_FULLSCREEN		= 'WhFs',
	// window.open / target=_blank: open this URL in a new window.
	// Carries B_WEB_URL_FIELD.
	B_WEB_PAGE_NEW_WINDOW		= 'WhNw',
	// JavaScript alert/confirm/prompt: reply message carries
	// B_WEB_DIALOG_ACCEPTED (bool) and B_WEB_DIALOG_TEXT (string).
	B_WEB_SCRIPT_ALERT			= 'WhSa',
	B_WEB_SCRIPT_CONFIRM		= 'WhSc',
	B_WEB_SCRIPT_PROMPT			= 'WhSp',
	// <input type=file>: reply carries B_WEB_FILE_REFS (refs array) or
	// is empty when the panel was cancelled. The request pointer rides
	// in B_WEB_FILE_REQUEST.
	B_WEB_FILE_CHOOSER			= 'WhFc',
	// Web notification permission for B_WEB_URL_FIELD site; reply has
	// B_WEB_PERMISSION_ALLOWED (bool).
	B_WEB_NOTIFICATION_ASK		= 'WhNa',
	// A web notification arrived; title in B_WEB_TITLE_FIELD, body in
	// B_WEB_MESSAGE_FIELD.
	B_WEB_NOTIFICATION_SHOW		= 'WhNs',
	// A download started/progressed/finished/failed. Fields:
	// B_WEB_DOWNLOAD_ID (uint64), B_WEB_DOWNLOAD_URL, _FILENAME,
	// _DESTINATION, B_WEB_DOWNLOAD_RECEIVED, B_WEB_DOWNLOAD_TOTAL;
	// FINISHED and FAILED carry _URL too, FAILED a _CANCELLED bool.
	B_WEB_DOWNLOAD_STARTED		= 'WhDs',
	B_WEB_DOWNLOAD_PROGRESS		= 'WhDp',
	B_WEB_DOWNLOAD_FINISHED		= 'WhDf',
	B_WEB_DOWNLOAD_FAILED		= 'WhDe',
	B_WEB_DOWNLOAD_ASK			= 'WhDa',
	// Navigation commands (context menu, app menus share these).
	B_WEB_NAV_BACK				= 'WhBk',
	B_WEB_NAV_FORWARD			= 'WhFw',
	B_WEB_NAV_RELOAD			= 'WhRl',
	B_WEB_NAV_STOP				= 'WhSt',
	B_WEB_NAV_HOME				= 'WhHm',
	// Edit commands from context/app menus.
	B_WEB_EDIT_CUT				= 'WhXt',
	B_WEB_EDIT_COPY				= 'WhCp',
	B_WEB_EDIT_PASTE			= 'WhPs',
	B_WEB_EDIT_SELECT_ALL		= 'WhAl',
	// Link/image actions.
	B_WEB_COPY_LINK				= 'WhCl',
	B_WEB_SAVE_LINK_AS			= 'WhSl',
	B_WEB_SAVE_IMAGE_AS			= 'WhSi',
	// Open the URL in B_WEB_URL_FIELD in a new window (or the current
	// one when "new_window" is false/absent).
	B_WEB_OPEN_LOCATION			= 'WhOl',
	// Internal: file/save panel finished; "chooser" points at the
	// pending request state.
	B_WEB_FILE_PANEL_DONE		= 'WhFp',
	B_WEB_SAVE_PANEL_DONE		= 'WhPv'
};



// Web page notification message field names
#define B_WEB_TITLE_FIELD		"web:title"
#define B_WEB_URL_FIELD			"web:url"
#define B_WEB_IMAGE_FIELD		"web:image"
#define B_WEB_PROGRESS_FIELD	"web:progress"
#define B_WEB_ERROR_FIELD		"web:error"
#define B_WEB_FRAME_FIELD		"web:frame"
#define B_WEB_MESSAGE_FIELD		"web:message"
#define B_WEB_CURSOR_FIELD		"web:cursor"
#define B_WEB_FULLSCREEN_FIELD	"web:fullscreen"
#define B_WEB_DIALOG_ACCEPTED	"web:accepted"
#define B_WEB_DIALOG_TEXT		"web:text"
#define B_WEB_FILE_REQUEST		"web:request"
#define B_WEB_FILE_REFS			"web:refs"
#define B_WEB_PERMISSION_ALLOWED "web:allowed"
#define B_WEB_DOWNLOAD_ID		"web:download_id"
#define B_WEB_DOWNLOAD_URL		"web:download_url"
#define B_WEB_DOWNLOAD_FILENAME	"web:download_filename"
#define B_WEB_DOWNLOAD_DESTINATION "web:download_destination"
#define B_WEB_DOWNLOAD_RECEIVED	"web:download_received"
#define B_WEB_DOWNLOAD_TOTAL	"web:download_total"
#define B_WEB_DOWNLOAD_CANCELLED "web:download_cancelled"
#define B_WEB_SUGGESTED_NAME	"web:suggested_name"


#endif // WEB_DEFS_H
