/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "PackageRow.h"

#include <new>
#include <string.h>

#include <Bitmap.h>
#include <Catalog.h>
#include <ColumnTypes.h>
#include <InterfaceDefs.h>
#include <View.h>

#include "PackageInfo.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PackageRow"


// The icon column carries both installed-state and mark state, so the
// enum encodes the combination, not package_mark alone.
enum glyph_state {
	kGlyphEmpty			= 0,	// not installed, unmarked
	kGlyphInstalled		= 1,	// installed, up to date, unmarked
	kGlyphUpgradable	= 2,	// installed, an upgrade is available, unmarked
	kGlyphMarkInstall	= 3,
	kGlyphMarkUpgrade	= 4,
	kGlyphMarkRemove	= 5,
	kGlyphMarkPurge		= 6,

	kGlyphStateCount	= 7
};


static glyph_state
glyph_state_for(const PackageInfo* package)
{
	if (is_upgrade_mark(package->Mark(), package->State()))
		return kGlyphMarkUpgrade;

	switch (package->Mark()) {
		case kMarkInstall:
			return kGlyphMarkInstall;
		case kMarkRemove:
			return kGlyphMarkRemove;
		case kMarkPurge:
			return kGlyphMarkPurge;
		default:
			break;
	}

	switch (package->State()) {
		case kPackageAvailable:
			return kGlyphEmpty;
		case kPackageUpgradable:
			return kGlyphUpgradable;
		default:
			return kGlyphInstalled;
	}
}


// One shared bitmap per (state, size); BBitmapField never deletes the
// bitmap it is handed, so reuse across rows is safe. Rebuilt when the
// row height changes. Red is removal, never "installed at rest".
static BBitmap*
combined_glyph(glyph_state state, float rowHeight)
{
	static BBitmap* sGlyphs[kGlyphStateCount] =
		{ NULL, NULL, NULL, NULL, NULL, NULL, NULL };
	static int32 sGlyphSize = -1;

	if (state < 0 || state >= kGlyphStateCount)
		return NULL;

	int32 size = (int32)rowHeight - 2;
	if (size < 10)
		size = 10;

	if (size != sGlyphSize) {
		for (int32 i = 0; i < kGlyphStateCount; i++) {
			delete sGlyphs[i];
			sGlyphs[i] = NULL;
		}
		sGlyphSize = size;
	}

	if (sGlyphs[state] != NULL)
		return sGlyphs[state];

	BRect bounds(0, 0, size - 1, size - 1);
	BBitmap* bitmap = new(std::nothrow) BBitmap(bounds,
		B_BITMAP_ACCEPTS_VIEWS, B_RGBA32);
	if (bitmap == NULL || bitmap->InitCheck() != B_OK) {
		delete bitmap;
		return NULL;
	}
	memset(bitmap->Bits(), 0, bitmap->BitsLength());

	if (bitmap->Lock()) {
		BView* view = new BView(bounds, "mark glyph", B_FOLLOW_NONE,
			B_WILL_DRAW);
		bitmap->AddChild(view);

		BRect box(2, 2, size - 3, size - 3);
		view->SetHighColor(ui_color(B_CONTROL_BORDER_COLOR));
		view->StrokeRect(box);
		view->SetPenSize(2);

		float mid = size / 2.0f;
		switch (state) {
			case kGlyphInstalled:
				view->SetHighColor(ui_color(B_CONTROL_MARK_COLOR));
				view->StrokeLine(BPoint(mid - 4, mid), BPoint(mid - 1, mid + 4));
				view->StrokeLine(BPoint(mid - 1, mid + 4), BPoint(mid + 5, mid - 4));
				break;
			case kGlyphUpgradable:
				// Still installed and calm; an up-arrow says an upgrade is waiting.
				view->SetHighColor(ui_color(B_CONTROL_MARK_COLOR));
				view->StrokeLine(BPoint(mid, mid + 5), BPoint(mid, mid - 3));
				view->StrokeLine(BPoint(mid - 4, mid - 1), BPoint(mid, mid - 5));
				view->StrokeLine(BPoint(mid + 4, mid - 1), BPoint(mid, mid - 5));
				break;
			case kGlyphMarkInstall:
				view->SetHighColor(ui_color(B_SUCCESS_COLOR));
				view->StrokeLine(BPoint(mid, mid - 5), BPoint(mid, mid + 5));
				view->StrokeLine(BPoint(mid - 5, mid), BPoint(mid + 5, mid));
				break;
			case kGlyphMarkUpgrade:
				// Green like install, but an arrow so the two stay distinct.
				view->SetHighColor(ui_color(B_SUCCESS_COLOR));
				view->StrokeLine(BPoint(mid, mid + 5), BPoint(mid, mid - 3));
				view->StrokeLine(BPoint(mid - 4, mid - 1), BPoint(mid, mid - 5));
				view->StrokeLine(BPoint(mid + 4, mid - 1), BPoint(mid, mid - 5));
				break;
			case kGlyphMarkRemove:
				view->SetHighColor(ui_color(B_FAILURE_COLOR));
				view->StrokeLine(BPoint(mid - 5, mid - 5), BPoint(mid + 5, mid + 5));
				view->StrokeLine(BPoint(mid + 5, mid - 5), BPoint(mid - 5, mid + 5));
				break;
			case kGlyphMarkPurge:
				view->SetHighColor(ui_color(B_FAILURE_COLOR));
				view->FillRect(box);
				view->SetHighColor(ui_color(B_CONTROL_BACKGROUND_COLOR));
				view->SetPenSize(3);
				view->StrokeLine(BPoint(mid - 5, mid - 5), BPoint(mid + 5, mid + 5));
				view->StrokeLine(BPoint(mid + 5, mid - 5), BPoint(mid - 5, mid + 5));
				break;
			default:
				break;
		}

		view->Sync();
		bitmap->Unlock();
	}

	sGlyphs[state] = bitmap;
	return bitmap;
}


static const char*
status_text(const PackageInfo* package)
{
	switch (package->Mark()) {
		case kMarkInstall:
			return B_TRANSLATE("Marked: install");
		case kMarkRemove:
			return B_TRANSLATE("Marked: remove");
		case kMarkPurge:
			return B_TRANSLATE("Marked: purge");
		default:
			break;
	}

	switch (package->State()) {
		case kPackageInstalled:
			return B_TRANSLATE("Installed");
		case kPackageUpgradable:
			return B_TRANSLATE("Upgradable");
		default:
			return B_TRANSLATE("Available");
	}
}


PackageRow::PackageRow(PackageInfo* package)
	:
	Inherited(),
	fPackage(package)
{
	Refresh();
}


PackageRow::~PackageRow()
{
}


package_mark
PackageRow::Mark() const
{
	if (fPackage == NULL)
		return kMarkNone;

	return fPackage->Mark();
}


void
PackageRow::SetMark(package_mark mark)
{
	if (fPackage == NULL)
		return;

	fPackage->SetMark(mark);
	Refresh();
}


void
PackageRow::Refresh()
{
	if (fPackage == NULL)
		return;

	SetField(new BBitmapField(
			combined_glyph(glyph_state_for(fPackage), Height())),
		kIconColumn);
	SetField(new BStringField(fPackage->Name().String()), kNameColumn);

	const BString& version = fPackage->State() == kPackageUpgradable
		? fPackage->CandidateVersion() : fPackage->Version();
	SetField(new BStringField(version.String()), kVersionColumn);

	SetField(new BStringField(fPackage->Category().String()), kSectionColumn);
	SetField(new BStringField(fPackage->ChannelLabel()), kChannelColumn);
	SetField(new BSizeField(fPackage->InstalledSize()), kSizeColumn);
	SetField(new BStringField(status_text(fPackage)), kStatusColumn);
}
