/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef ARRANGEMENT_VIEW_H
#define ARRANGEMENT_VIEW_H


#include <ObjectList.h>
#include <Rect.h>
#include <String.h>
#include <View.h>


// Posted to the window: "name" of the output that was clicked.
static const uint32 kMsgOutputSelected = 'osel';
// Posted to the window after a drag: "screen" and "origin" for each
// enabled output.
static const uint32 kMsgLayoutDropped = 'ldrp';


struct output_entry {
	BString		name;
	bool		enabled;
	bool		first;
	bool		main;
	int32		screen;
	BRect		frame;
};


class ArrangementView : public BView {
public:
							ArrangementView(const char* name);
	virtual					~ArrangementView();

	virtual	void			GetPreferredSize(float* _width, float* _height);
	virtual	void			AttachedToWindow();
	virtual	void			Draw(BRect updateRect);
	virtual	void			MouseDown(BPoint where);
	virtual	void			MouseMoved(BPoint where, uint32 transit,
								const BMessage* dragMessage);
	virtual	void			MouseUp(BPoint where);

			// From BPrivateScreen::GetOutputs().
			void			SetOutputs(const BMessage& outputs);
			void			Select(const char* name);
			const char*		Selected() const { return fSelected.String(); }
			int32			CountOutputs() const
								{ return fOutputs.CountItems(); }
			const output_entry*	OutputAt(int32 index) const
								{ return fOutputs.ItemAt(index); }
			const output_entry*	Find(const char* name) const;
			bool			IsMirrored() const;

private:
			float			_Scale() const;
			BRect			_BoxFor(const output_entry* output,
								const BRect& desktop, float scale) const;
			BRect			_DisabledBox(int32 index) const;
			BRect			_DesktopBounds() const;
			output_entry*	_HitTest(BPoint where, bool cycle) const;
			BRect			_Placement(int32 dragged, BRect wanted) const;
			void			_Notify(uint32 what);

			BObjectList<output_entry, true>	fOutputs;
			BString			fSelected;

			// Drag in progress, in desktop coordinates.
			int32			fDragIndex;
			BPoint			fDragStart;
			BRect			fDragOrigin;
			BRect			fDragFrame;
			bool			fDragMoved;
};


#endif	// ARRANGEMENT_VIEW_H
