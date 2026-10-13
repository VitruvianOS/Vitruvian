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
	B_WEB_FRAME_READY			= 'WhFr'
};


// Web page notification message field names
#define B_WEB_TITLE_FIELD		"web:title"
#define B_WEB_URL_FIELD			"web:url"
#define B_WEB_PROGRESS_FIELD	"web:progress"
#define B_WEB_ERROR_FIELD		"web:error"
#define B_WEB_FRAME_FIELD		"web:frame"
#define B_WEB_MESSAGE_FIELD		"web:message"


#endif // WEB_DEFS_H
