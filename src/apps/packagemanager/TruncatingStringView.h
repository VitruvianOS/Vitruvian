/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef TRUNCATING_STRING_VIEW_H
#define TRUNCATING_STRING_VIEW_H

#include <Font.h>
#include <String.h>
#include <StringView.h>


// A BStringView that keeps the full text and truncates only what it
// draws; bounds at SetFullText() time can be far smaller than the
// laid-out width will be, so never overwrite the stored text.
class TruncatingStringView : public BStringView {
	typedef BStringView Inherited;
public:
	TruncatingStringView(const char* name, const char* text = "")
		:
		Inherited(name, text),
		fFullText(text)
	{
	}

	void SetFullText(const char* text)
	{
		fFullText = text;
		Inherited::SetText(text);
		Invalidate();
	}

	const BString& FullText() const
	{
		return fFullText;
	}

	// Without a minimum, an AddGlue() sibling absorbs every pixel of
	// surplus (BGroupLayout weights by max-min) and starves the label.
	void SetMinWidthChars(int32 chars)
	{
		BFont font;
		GetFont(&font);
		SetExplicitMinSize(BSize(font.StringWidth("m") * chars,
			B_SIZE_UNSET));
	}

	// Truncate at draw time against the actual width; FrameResized()
	// depends on layout ordering and can leave the text permanently cut.
	virtual void Draw(BRect updateRect)
	{
		BString text(fFullText);
		BFont font;
		GetFont(&font);

		float width = Bounds().Width();
		font.TruncateString(&text, B_TRUNCATE_END, width);

		font_height height;
		font.GetHeight(&height);

		float x = 0;
		float textWidth = font.StringWidth(text.String());
		switch (Alignment()) {
			case B_ALIGN_RIGHT:
				x = width - textWidth;
				break;
			case B_ALIGN_CENTER:
				x = (width - textWidth) / 2;
				break;
			default:
				break;
		}

		SetDrawingMode(B_OP_OVER);
		DrawString(text.String(), BPoint(x, ceilf(height.ascent)));
	}

private:
	BString fFullText;
};


#endif // TRUNCATING_STRING_VIEW_H
