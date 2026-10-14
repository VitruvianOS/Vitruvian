/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SCAN_VIEW_H
#define _SCAN_VIEW_H


#include <View.h>


class BBitmap;


// Preview pane with a draggable selection rectangle. The selection is
// kept in bitmap pixel coordinates; the window converts it to inches.
class ScanView : public BView {
public:
					ScanView();

			void	SetBitmap(BBitmap* bitmap);
			BBitmap* Bitmap() const { return fBitmap; }

			bool	HasSelection() const { return fHasSelection; }
			BRect	Selection() const;

private:
	virtual	void	Draw(BRect updateRect);
	virtual	void	MouseDown(BPoint where);
	virtual	void	MouseUp(BPoint where);
	virtual	void	MouseMoved(BPoint where, uint32 transit,
						const BMessage* dragMessage);

			void	_TrackSelection(BPoint where);

			BBitmap*	fBitmap;
			bool		fHasSelection;
			bool		fDragging;
			BPoint		fAnchor;
			BRect		fSelection;
};


#endif	// _SCAN_VIEW_H
