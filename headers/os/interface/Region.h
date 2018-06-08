//------------------------------------------------------------------------------
//	Copyright (c) 2003, OpenBeOS
//
//	Permission is hereby granted, free of charge, to any person obtaining a
//	copy of this software and associated documentation files (the "Software"),
//	to deal in the Software without restriction, including without limitation
//	the rights to use, copy, modify, merge, publish, distribute, sublicense,
//	and/or sell copies of the Software, and to permit persons to whom the
//	Software is furnished to do so, subject to the following conditions:
//
//	The above copyright notice and this permission notice shall be included in
//	all copies or substantial portions of the Software.
//
//	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//	DEALINGS IN THE SOFTWARE.
//
//	File Name:		Region.h
//	Author:			Stefano Ceccherini (burton666@libero.it)
//	Description:	Region class consisting of multiple rectangles
//
//------------------------------------------------------------------------------


#ifndef	_REGION_H
#define	_REGION_H

#include <BeBuild.h>
#include <Rect.h>

/* Integer rect used to define a cliping rectangle. All bounds are included */
/* Moved from DirectWindow.h */
typedef struct {
	int32		left;
	int32		top;
	int32		right;
	int32		bottom;
} clipping_rect;


/*----------------------------------------------------------------*/
/*----- BRegion class --------------------------------------------*/

class BRegion {

public:
				BRegion();
				BRegion(const BRegion &region);
				BRegion(const BRect rect);
virtual			~BRegion();	

		BRegion	&operator=(const BRegion &from);

		BRect	Frame() const;
clipping_rect	FrameInt() const;
		BRect	RectAt(int32 index);
clipping_rect	RectAtInt(int32 index);
		int32	CountRects();
		void	Set(BRect newBounds);
		void	Set(clipping_rect newBounds);
		bool	Intersects(BRect r) const;
		bool	Intersects(clipping_rect r) const;
		bool	Contains(BPoint pt) const;
		bool	Contains(int32 x, int32 y);
		void	PrintToStream() const;
		void	OffsetBy(int32 dh, int32 dv);
		void	MakeEmpty();
		void	Include(BRect r);
		void	Include(clipping_rect r);
		void	Include(const BRegion*);
		void	Exclude(BRect r);
		void	Exclude(clipping_rect r);
		void	Exclude(const BRegion*);
		void	IntersectWith(const BRegion*);

/*----- Private or reserved -----------------------------------------*/
		class Support;

private:

friend class BView;
friend class BDirectWindow;
friend class Support;

		void	_AddRect(clipping_rect r);
		void	set_size(long new_size);

private:
		long	count;
		long	data_size;
		clipping_rect	bound;
		clipping_rect	*data;
};

/*-------------------------------------------------------------*/
/*-------------------------------------------------------------*/

#endif /* _REGION_H */
