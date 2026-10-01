/*
 * Copyright 2026, VitruvianOS Project.
 * Distributed under the terms of the LGPL License.
 */
#ifndef _WEB_DEFS_H
#define _WEB_DEFS_H


#include <SupportDefs.h>


// Web page notification message what-codes
enum {
	B_WEB_PAGE_TITLE_CHANGED		= 'Wtch',
	B_WEB_PAGE_URL_CHANGED			= 'Wuch',
	B_WEB_PAGE_PROGRESS_CHANGED		= 'Wprg',
	B_WEB_PAGE_LOAD_FINISHED		= 'Wldn',
	B_WEB_PAGE_LOAD_FAILED			= 'Wldr',
	B_WEB_PAGE_FRAME_CHANGED		= 'Wfch',
	B_WEB_PAGE_NAVIGATION_STARTED	= 'Wnst',
	B_WEB_PAGE_NAVIGATION_FAILED	= 'Wnsf',
	B_WEB_PAGE_DOWNLOAD_STARTED		= 'Wdls',
	B_WEB_PAGE_ALERT				= 'Walt',
	B_WEB_PAGE_CONFIRM				= 'Wcnf',
	B_WEB_PAGE_PROMPT				= 'Wpmt',
	B_WEB_NEW_WINDOW_REQUESTED		= 'Wnwr',
	B_WEB_PAGE_TITLE_CHANGED	= 'WhTt',
	B_WEB_PAGE_URL_CHANGED		= 'WhUc',
	B_WEB_PAGE_PROGRESS_CHANGED	= 'WhPr',
	B_WEB_PAGE_LOAD_FINISHED	= 'WhLf',
	B_WEB_PAGE_LOAD_FAILED		= 'WhFl',
	B_WEB_FRAME_READY			= 'WhFr'
};

// Web page notification message field names
extern const char* const B_WEB_TITLE_FIELD;
extern const char* const B_WEB_URL_FIELD;
extern const char* const B_WEB_PROGRESS_FIELD;
extern const char* const B_WEB_ERROR_FIELD;
extern const char* const B_WEB_FRAME_FIELD;
extern const char* const B_WEB_MESSAGE_FIELD;
#define B_WEB_TITLE_FIELD		"web:title"
#define B_WEB_URL_FIELD			"web:url"
#define B_WEB_PROGRESS_FIELD	"web:progress"
#define B_WEB_ERROR_FIELD		"web:error"
#define B_WEB_FRAME_FIELD		"web:frame"
#define B_WEB_MESSAGE_FIELD		"web:message"


#endif /* _WEB_DEFS_H */
