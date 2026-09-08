/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "PackageInfoView.h"

#include <Bitmap.h>
#include <Button.h>
#include <Catalog.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <MimeType.h>
#include <NodeInfo.h>
#include <ObjectList.h>
#include <OutlineListView.h>
#include <ScrollView.h>
#include <StringForSize.h>
#include <StringItem.h>
#include <TabView.h>
#include <TextView.h>
#include <Window.h>

#include "PackageInfo.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PackageInfoView"


static const float kIconSize = 16.0f;


class ContentsItem : public BStringItem {
public:
	enum kind {
		kRoot,
		kMeta,
		kDir,
		kFile,
		kDep
	};

							ContentsItem(const BString& text, kind type,
								PackageInfoView* view,
								const BString& path = "");

	virtual	void			DrawItem(BView* owner, BRect frame,
								bool complete);

			kind			Type() const
								{ return fType; }
			const BString&	Path() const
								{ return fPath; }

protected:
			PackageInfoView* fView;
			kind			fType;
			BString			fPath;
			BBitmap*		fIcon;
};


ContentsItem::ContentsItem(const BString& text, kind type,
	PackageInfoView* view, const BString& path)
	:
	BStringItem(text),
	fView(view),
	fType(type),
	fPath(path),
	fIcon(NULL)
{
}


void
ContentsItem::DrawItem(BView* owner, BRect frame, bool complete)
{
	if (fIcon == NULL && fView != NULL)
		fIcon = fView->_IconFor(this);

	if (IsSelected()) {
		owner->SetLowUIColor(B_LIST_SELECTED_BACKGROUND_COLOR);
		owner->FillRect(frame, B_SOLID_LOW);
		owner->SetHighUIColor(B_LIST_SELECTED_ITEM_TEXT_COLOR);
	} else {
		if (complete) {
			owner->SetLowUIColor(B_LIST_BACKGROUND_COLOR);
			owner->FillRect(frame, B_SOLID_LOW);
		}
		owner->SetHighUIColor(B_LIST_ITEM_TEXT_COLOR);
	}

	float textInset = 4.0f;
	if (fIcon != NULL) {
		owner->SetDrawingMode(B_OP_ALPHA);
		owner->DrawBitmap(fIcon, BPoint(frame.left + 2.0f,
			frame.top + (frame.Height() - kIconSize) / 2.0f));
		owner->SetDrawingMode(B_OP_COPY);
		textInset += kIconSize;
	}

	BString text(Text());
	be_plain_font->TruncateString(&text, B_TRUNCATE_END,
		frame.Width() - textInset - 4.0f);
	owner->MovePenTo(frame.left + textInset,
		frame.top + 4.0f + be_plain_font->Size());
	owner->DrawString(text.String());
}


class ContentsListView : public BOutlineListView {
	typedef BOutlineListView Inherited;
public:
							ContentsListView();
};


ContentsListView::ContentsListView()
	:
	Inherited("contents")
{
}


static int
compare_paths(const BString* a, const BString* b)
{
	return strcmp(a->String(), b->String());
}


static BTextView*
make_read_only_text_view(const char* name)
{
	BTextView* view = new BTextView(name);
	view->MakeEditable(false);
	view->SetStylable(true);
	view->SetWordWrap(true);
	view->SetInsets(6, 6, 6, 6);
	return view;
}


PackageInfoView::PackageInfoView()
	:
	Inherited("package info", B_VERTICAL, B_USE_SMALL_SPACING),
	fPackage(NULL),
	fTransactionActive(false)
{
	fTitleView = new TruncatingStringView("title", "");
	fTitleView->SetFont(be_bold_font);
	fTitleView->SetMinWidthChars(24);
	fVersionView = new TruncatingStringView("version", "");
	fVersionView->SetMinWidthChars(16);
	fChannelView = new TruncatingStringView("channel", "");
	fChannelView->SetMinWidthChars(12);

	// The label and target message are re-derived per package; see
	// _UpdateSelectButton().
	fSelectButton = new BButton("select", B_TRANSLATE("Select"),
		new BMessage(kMsgMarkInstall));
	fSelectButton->SetEnabled(false);

	fApplyButton = new BButton("apply", B_TRANSLATE("Apply"),
		new BMessage(kMsgApply));
	fApplyButton->SetEnabled(false);

	fDescriptionView = make_read_only_text_view("description");
	fContentsView = new ContentsListView();
	fChangelogView = make_read_only_text_view("changelog");

	fTabView = new BTabView("detail tabs", B_WIDTH_FROM_LABEL);
	fTabView->AddTab(new BScrollView("description scroll", fDescriptionView,
		0, false, true, B_NO_BORDER));
	fTabView->TabAt(0)->SetLabel(B_TRANSLATE("Description"));
	fTabView->AddTab(new BScrollView("contents scroll", fContentsView,
		0, false, true, B_NO_BORDER));
	fTabView->TabAt(1)->SetLabel(B_TRANSLATE("Contents"));
	fTabView->AddTab(new BScrollView("changelog scroll", fChangelogView,
		0, false, true, B_NO_BORDER));
	fTabView->TabAt(2)->SetLabel(B_TRANSLATE("Changelog"));

	BLayoutBuilder::Group<>(this)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fTitleView)
			.Add(fVersionView)
			.Add(fChannelView)
			.AddGlue()
			.Add(fSelectButton)
			.Add(fApplyButton)
		.End()
		.Add(fTabView);

	Clear();
}


PackageInfoView::~PackageInfoView()
{
	_FreeIconCache();
}


void
PackageInfoView::AttachedToWindow()
{
	Inherited::AttachedToWindow();

	fSelectButton->SetTarget(Window());
	fApplyButton->SetTarget(Window());
}


void
PackageInfoView::SetPackage(PackageInfo* package)
{
	fPackage = package;
	if (package == NULL) {
		Clear();
		return;
	}

	fTitleView->SetFullText(package->Name().String());
	fVersionView->SetFullText(package->Version().String());
	fChannelView->SetFullText(package->ChannelLabel());
	fDescriptionView->SetText(package->Summary().String());
	// Replies land later; drop the previous package's data now.
	fContentsView->MakeEmpty();
	fChangelogView->SetText("");
	_UpdateSelectButton();
}


void
PackageInfoView::SetDetails(const BMessage* details)
{
	if (details == NULL)
		return;

	// Drop a stale reply for a since-deselected package.
	const char* name = NULL;
	if (details->FindString("name", &name) != B_OK || fPackage == NULL
			|| fPackage->Name() != name) {
		return;
	}

	const char* description = "";
	details->FindString("description", &description);
	const char* version = "";
	details->FindString("version", &version);
	const char* section = "";
	details->FindString("section", &section);
	int64 installedSize = 0;
	details->FindInt64("installed_size", &installedSize);
	const char* depends = "";
	details->FindString("depends", &depends);

	char sizeText[64];
	string_for_size((double)installedSize, sizeText, sizeof(sizeText));

	BString text(description);
	text << "\n\n";
	text << B_TRANSLATE("Version: ") << version << "\n";
	text << B_TRANSLATE("Section: ") << section << "\n";
	text << B_TRANSLATE("Installed size: ") << sizeText << "\n";
	if (BString(depends).Length() > 0)
		text << B_TRANSLATE("Depends: ") << depends << "\n";
	fDescriptionView->SetText(text.String());

	_BuildContentsTree(details);
}


void
PackageInfoView::SetChangelog(const BMessage* changelog)
{
	if (changelog == NULL)
		return;

	const char* name = NULL;
	if (changelog->FindString("name", &name) != B_OK || fPackage == NULL
			|| fPackage->Name() != name) {
		return;
	}

	const char* text = "";
	changelog->FindString("text", &text);
	fChangelogView->SetText(text);
}


void
PackageInfoView::Clear()
{
	fTitleView->SetFullText(B_TRANSLATE("No package selected"));
	fVersionView->SetFullText("");
	fChannelView->SetFullText("");
	fDescriptionView->SetText("");
	fChangelogView->SetText("");
	fContentsView->MakeEmpty();
	_UpdateSelectButton();
}


void
PackageInfoView::SetTransactionActive(bool active)
{
	fTransactionActive = active;
	_UpdateSelectButton();
}


void
PackageInfoView::RefreshSelectButton()
{
	_UpdateSelectButton();
}


void
PackageInfoView::SetApplyState(int32 pendingCount, bool enabled)
{
	if (pendingCount > 0) {
		BString label(B_TRANSLATE("Apply %count% changes"));
		BString countText;
		countText << pendingCount;
		label.ReplaceFirst("%count%", countText);
		fApplyButton->SetLabel(label.String());
	} else {
		fApplyButton->SetLabel(B_TRANSLATE("Apply"));
	}

	fApplyButton->SetEnabled(enabled);
}


void
PackageInfoView::_UpdateSelectButton()
{
	if (fPackage == NULL || fTransactionActive) {
		fSelectButton->SetLabel(B_TRANSLATE("Select"));
		fSelectButton->SetEnabled(false);
		return;
	}

	// A mark always wins: Deselect only clears the pending mark, it
	// never touches the system. Unmarked: State() picks the action;
	// upgradable still posts kMarkInstall, apt's install verb performs
	// the upgrade.
	if (fPackage->Mark() != kMarkNone) {
		fSelectButton->SetLabel(B_TRANSLATE("Deselect"));
		fSelectButton->SetMessage(new BMessage(kMsgUnmark));
	} else if (fPackage->State() == kPackageInstalled) {
		fSelectButton->SetLabel(B_TRANSLATE("Select for Uninstall"));
		fSelectButton->SetMessage(new BMessage(kMsgMarkRemove));
	} else if (fPackage->State() == kPackageUpgradable) {
		fSelectButton->SetLabel(B_TRANSLATE("Select for Update"));
		fSelectButton->SetMessage(new BMessage(kMsgMarkInstall));
	} else {
		fSelectButton->SetLabel(B_TRANSLATE("Select for Install"));
		fSelectButton->SetMessage(new BMessage(kMsgMarkInstall));
	}

	fSelectButton->SetEnabled(true);
}


void
PackageInfoView::_BuildContentsTree(const BMessage* details)
{
	fContentsView->MakeEmpty();
	_FreeIconCache();

	ContentsItem* dataRoot = new ContentsItem(B_TRANSLATE("data"),
		ContentsItem::kRoot, this);
	ContentsItem* metaRoot = new ContentsItem(B_TRANSLATE("metadata"),
		ContentsItem::kRoot, this);
	fContentsView->AddItem(dataRoot);
	fContentsView->AddItem(metaRoot);

	_AddMetaRow(metaRoot, B_TRANSLATE("Name"), fPackage->Name());
	_AddMetaRow(metaRoot, B_TRANSLATE("Installed version"),
		fPackage->Version());
	_AddMetaRow(metaRoot, B_TRANSLATE("Candidate version"),
		fPackage->CandidateVersion());
	_AddMetaRow(metaRoot, B_TRANSLATE("Architecture"),
		fPackage->Architecture());

	const char* section = "";
	details->FindString("section", &section);
	_AddMetaRow(metaRoot, B_TRANSLATE("Section"), section);

	_AddMetaRow(metaRoot, B_TRANSLATE("Channel"),
		fPackage->ChannelLabel());

	int64 installedSize = 0;
	details->FindInt64("installed_size", &installedSize);
	char sizeText[64];
	string_for_size((double)installedSize, sizeText, sizeof(sizeText));
	_AddMetaRow(metaRoot, B_TRANSLATE("Installed size"), sizeText);

	char downloadText[64];
	string_for_size((double)fPackage->DownloadSize(), downloadText,
		sizeof(downloadText));
	_AddMetaRow(metaRoot, B_TRANSLATE("Download size"), downloadText);

	_AddMetaRow(metaRoot, B_TRANSLATE("Summary"), fPackage->Summary());

	BObjectList<BString, true> paths(1024);
	type_code type;
	int32 count = 0;
	if (details->GetInfo("path", &type, &count) == B_OK) {
		for (int32 i = 0; i < count; i++) {
			const char* path = NULL;
			if (details->FindString("path", i, &path) == B_OK
					&& path[0] == '/' && path[1] != '\0')
				paths.AddItem(new BString(path));
		}
	}

	// Sorted order puts every directory before the entries below it.
	paths.SortItems(compare_paths);

	HashMap<HashString, ContentsItem*> dirs;
	for (int32 i = 0; i < paths.CountItems(); i++) {
		const BString& path = *paths.ItemAt(i);
		int32 lastSlash = path.FindLast('/');

		ContentsItem* parent = dataRoot;
		if (lastSlash > 0)
			parent = _EnsureDir(dirs, BString(path.String(), lastSlash),
				dataRoot);

		// The path itself may already exist as a directory entry.
		if (dirs.ContainsKey(HashString(path.String())))
			continue;

		fContentsView->AddUnder(new ContentsItem(
			path.String() + lastSlash + 1, ContentsItem::kFile, this, path),
			parent);
	}
}


ContentsItem*
PackageInfoView::_EnsureDir(HashMap<HashString, ContentsItem*>& dirs,
	const BString& prefix, ContentsItem* dataRoot)
{
	ContentsItem* existing = dirs.Get(HashString(prefix.String()));
	if (existing != NULL)
		return existing;

	int32 lastSlash = prefix.FindLast('/');
	ContentsItem* parent = dataRoot;
	if (lastSlash > 0)
		parent = _EnsureDir(dirs, BString(prefix.String(), lastSlash),
			dataRoot);

	BString label;
	prefix.CopyInto(label, lastSlash + 1, prefix.Length() - lastSlash - 1);

	ContentsItem* dir = new ContentsItem(label, ContentsItem::kDir, this,
		prefix);
	fContentsView->AddUnder(dir, parent);
	dirs.Put(HashString(prefix.String()), dir);
	return dir;
}


void
PackageInfoView::_AddMetaRow(ContentsItem* metaRoot, const char* label,
	const BString& value)
{
	BString text(label);
	text << ": " << value;
	fContentsView->AddUnder(new ContentsItem(text, ContentsItem::kMeta, this),
		metaRoot);
}


BBitmap*
PackageInfoView::_IconFor(ContentsItem* item)
{
	if (item->Path().IsEmpty())
		return NULL;

	BBitmap* cached = fIconCache.Get(HashString(item->Path().String()));
	if (cached != NULL)
		return cached;

	BBitmap* icon = new BBitmap(
		BRect(0, 0, kIconSize - 1, kIconSize - 1), B_RGBA32);
	BEntry entry(item->Path().String());
	entry_ref ref;
	bool fetched = entry.Exists() && entry.GetRef(&ref) == B_OK
		&& BNodeInfo::GetTrackerIcon(&ref, icon, B_MINI_ICON) == B_OK;
	if (!fetched) {
		delete icon;
		BMimeType type;
		if (BMimeType::GuessMimeType(item->Path().String(), &type) != B_OK)
			type.SetTo(B_FILE_MIME_TYPE);
		icon = new BBitmap(
			BRect(0, 0, kIconSize - 1, kIconSize - 1), B_RGBA32);
		fetched = type.GetIcon(icon, B_MINI_ICON) == B_OK;
		if (!fetched) {
			delete icon;
			return NULL;
		}
	}

	fIconCache.Put(HashString(item->Path().String()), icon);
	return icon;
}


void
PackageInfoView::_FreeIconCache()
{
	HashMap<HashString, BBitmap*>::Iterator it = fIconCache.GetIterator();
	while (it.HasNext())
		delete it.Next().value;
	fIconCache.Clear();
}
