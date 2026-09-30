/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _PRINTCUPS_RENDER_H
#define _PRINTCUPS_RENDER_H


#include <List.h>
#include <Point.h>
#include <Rect.h>
#include <SupportDefs.h>


class BBitmap;
class BPicture;


struct SpooledPicture {
	BPicture*	picture;
	BRect		bounds;
	BPoint		where;
};


struct SpooledPage {
	BList	pictures;	// SpooledPicture*
};


extern status_t	printcups_render_pages_pdf(const char* pdfPath, BList* pages,
					float paperWidth, float paperHeight, float scale,
					const char* title);
extern status_t	printcups_render_bitmap_fallback(const char* pdfPath,
					BBitmap* bitmap, float paperWidth, float paperHeight,
					float scale);

extern SpooledPage*	printcups_new_page();
extern void			printcups_add_picture(SpooledPage* page, BPicture* picture,
					const BRect& bounds, const BPoint& where);
extern void			printcups_free_page(SpooledPage* page);


#endif // _PRINTCUPS_RENDER_H
