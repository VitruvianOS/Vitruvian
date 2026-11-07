/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "RepositoriesView.h"

#include <new>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <Button.h>
#include <Catalog.h>
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <Directory.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <Path.h>
#include <Size.h>

#include "PackageManagerDefs.h"
#include "TruncatingStringView.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "RepositoriesView"


// The Reload button talks to this view only, never the window.
enum { kMsgReloadSources = 'rvrl' };


enum {
	kEnabledColumn		= 0,
	kTypesColumn		= 1,
	kUrisColumn			= 2,
	kSuitesColumn		= 3,
	kComponentsColumn	= 4,
	kSignedByColumn		= 5,
	kFileColumn			= 6
};


// Rows borrow the entry; the view owns the model.
class SourceRow : public BRow {
	typedef BRow Inherited;
public:
								SourceRow(source_entry* entry);

			source_entry*		Entry() const
									{ return fEntry; }

private:
			source_entry*		fEntry;
};


SourceRow::SourceRow(source_entry* entry)
	:
	Inherited(),
	fEntry(entry)
{
}


static source_entry*
new_entry()
{
	source_entry* entry = new(std::nothrow) source_entry;
	if (entry == NULL)
		return NULL;

	entry->index = 0;
	entry->enabled = true;
	entry->oneLine = false;
	entry->owned = false;
	return entry;
}


static bool
has_suffix(const BString& name, const char* suffix)
{
	const size_t length = strlen(suffix);
	if ((size_t)name.Length() < length)
		return false;
	return strcmp(name.String() + name.Length() - length, suffix) == 0;
}


// Mirrors the helper's ownership rule: only vos-user-*.sources may be
// edited or removed from this UI.
static bool
is_owned_name(const BString& name)
{
	return has_suffix(name, ".sources") && name.Length() > 17
		&& strncmp(name.String(), "vos-user-", 9) == 0;
}


static int
compare_paths(const BString* a, const BString* b)
{
	return a->Compare(*b);
}


static bool
read_file(const char* path, BString* out)
{
	FILE* file = fopen(path, "r");
	if (file == NULL)
		return false;

	char buffer[4096];
	size_t bytesRead;
	while ((bytesRead = fread(buffer, 1, sizeof(buffer), file)) > 0)
		out->Append(buffer, bytesRead);
	fclose(file);
	return true;
}


static bool
is_space(char c)
{
	return c == ' ' || c == '\t';
}


// deb [options] uri suite [components...] on one physical line.
static source_entry*
parse_one_line(const BString& path, const BString& name, int32 lineNumber,
	const BString& text)
{
	BString trimmed(text);
	trimmed.Trim();
	if (trimmed.IsEmpty())
		return NULL;

	bool enabled = true;
	BString body(trimmed);
	if (body.ByteAt(0) == '#') {
		enabled = false;
		int32 at = 1;
		while (at < body.Length() && is_space(body.ByteAt(at)))
			at++;
		body.Remove(0, at);
		body.Trim();
	}

	int32 typeLength = -1;
	if (body.StartsWith("deb-src "))
		typeLength = 8;
	else if (body.StartsWith("deb "))
		typeLength = 4;
	else
		return NULL;

	source_entry* entry = new_entry();
	if (entry == NULL)
		return NULL;

	entry->path = path;
	entry->name = name;
	entry->index = lineNumber;
	entry->enabled = enabled;
	entry->oneLine = true;
	entry->types = typeLength == 8 ? "deb-src" : "deb";

	const int32 length = body.Length();
	int32 pos = typeLength;
	while (pos < length && is_space(body.ByteAt(pos)))
		pos++;

	if (pos < length && body.ByteAt(pos) == '[') {
		const int32 close = body.FindFirst(']', pos);
		if (close < 0) {
			delete entry;
			return NULL;
		}
		BString options;
		body.CopyInto(options, pos + 1, close - pos - 1);
		pos = close + 1;
		while (pos < length && is_space(body.ByteAt(pos)))
			pos++;

		int32 at = 0;
		while (at < options.Length()) {
			while (at < options.Length() && is_space(options.ByteAt(at)))
				at++;
			int32 end = at;
			while (end < options.Length() && !is_space(options.ByteAt(end)))
				end++;
			if (end > at) {
				BString option;
				options.CopyInto(option, at, end - at);
				if (option.IStartsWith("signed-by="))
					entry->signedBy = option.String() + 10;
			}
			at = end;
		}
	}

	int32 start = pos;
	while (pos < length && !is_space(body.ByteAt(pos)))
		pos++;
	if (pos == start) {
		delete entry;
		return NULL;
	}
	body.CopyInto(entry->uris, start, pos - start);

	while (pos < length && is_space(body.ByteAt(pos)))
		pos++;
	start = pos;
	while (pos < length && !is_space(body.ByteAt(pos)))
		pos++;
	if (pos == start) {
		delete entry;
		return NULL;
	}
	body.CopyInto(entry->suites, start, pos - start);

	BString rest;
	body.CopyInto(rest, pos, length - pos);
	rest.Trim();
	entry->components = rest;

	return entry;
}


// One deb822 stanza collected as raw lines; continuation lines append
// to the value of the field they belong to.
static source_entry*
parse_stanza(const BString& path, const BString& name, int32 stanzaNumber,
	const BObjectList<BString, true>& lines)
{
	source_entry* entry = new_entry();
	if (entry == NULL)
		return NULL;

	entry->path = path;
	entry->name = name;
	entry->index = stanzaNumber;

	BString* lastTarget = NULL;
	for (int32 i = 0; i < lines.CountItems(); i++) {
		const BString& line = *lines.ItemAt(i);
		if (line.IsEmpty())
			continue;

		if (is_space(line.ByteAt(0))) {
			if (lastTarget != NULL) {
				BString continuation(line);
				continuation.Trim();
				if (continuation.Length() > 0)
					*lastTarget << " " << continuation;
			}
			continue;
		}

		const int32 colon = line.FindFirst(':');
		if (colon <= 0)
			continue;

		BString field;
		BString value;
		line.CopyInto(field, 0, colon);
		line.CopyInto(value, colon + 1, line.Length() - colon - 1);
		value.Trim();

		lastTarget = NULL;
		if (field.ICompare("Types") == 0)
			lastTarget = &entry->types;
		else if (field.ICompare("URIs") == 0)
			lastTarget = &entry->uris;
		else if (field.ICompare("Suites") == 0)
			lastTarget = &entry->suites;
		else if (field.ICompare("Components") == 0)
			lastTarget = &entry->components;
		else if (field.ICompare("Signed-By") == 0)
			lastTarget = &entry->signedBy;
		else if (field.ICompare("Enabled") == 0) {
			if (value.ICompare("no") == 0 || value.ICompare("false") == 0
					|| value == "0") {
				entry->enabled = false;
			}
			continue;
		} else
			continue;

		if (lastTarget->Length() > 0)
			*lastTarget << " " << value;
		else
			*lastTarget = value;
	}

	// A stanza without URIs (comments, half-written file) is not a
	// source apt would use; leave it out of the list.
	if (entry->uris.IsEmpty()) {
		delete entry;
		return NULL;
	}

	return entry;
}


RepositoriesView::RepositoriesView()
	:
	Inherited("repositories", B_VERTICAL, B_USE_SMALL_SPACING),
	fTransactionActive(false)
{
	fHintView = new TruncatingStringView("repositories hint",
		B_TRANSLATE("User-added sources can be edited and removed. "
			"System sources can only be enabled or disabled."));
	fHintView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fHintView->SetExplicitMinSize(BSize(0, B_SIZE_UNSET));

	fListView = new BColumnListView("sources", B_NAVIGABLE, B_FANCY_BORDER,
		true);
	fListView->SetSelectionMode(B_SINGLE_SELECTION_LIST);
	fListView->SetSortingEnabled(false);
	fListView->SetSelectionMessage(new BMessage(kMsgSourceSelectionChanged));
	fListView->SetExplicitMinSize(BSize(400, 160));
	_AddColumns();

	fAddButton = new BButton("add", B_TRANSLATE("Add..."),
		new BMessage(kMsgSourceAddRequested));
	fEditButton = new BButton("edit", B_TRANSLATE("Edit..."),
		new BMessage(kMsgSourceEditRequested));
	fRemoveButton = new BButton("remove", B_TRANSLATE("Remove"),
		new BMessage(kMsgSourceRemoveRequested));
	fToggleButton = new BButton("toggle", B_TRANSLATE("Enable/disable"),
		new BMessage(kMsgSourceToggleRequested));
	fReloadButton = new BButton("reload", B_TRANSLATE("Reload"),
		new BMessage(kMsgReloadSources));

	BLayoutBuilder::Group<>(this)
		.SetInsets(B_USE_SMALL_SPACING)
		.Add(fHintView)
		.Add(fListView)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fAddButton)
			.Add(fEditButton)
			.Add(fRemoveButton)
			.Add(fToggleButton)
			.AddGlue()
			.Add(fReloadButton)
		.End();

	Reload();
}


RepositoriesView::~RepositoriesView()
{
}


void
RepositoriesView::AttachedToWindow()
{
	Inherited::AttachedToWindow();

	fAddButton->SetTarget(Window());
	fEditButton->SetTarget(Window());
	fRemoveButton->SetTarget(Window());
	fToggleButton->SetTarget(Window());
	fReloadButton->SetTarget(this);
}


void
RepositoriesView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgReloadSources:
			Reload();
			break;

		case kMsgSourceSelectionChanged:
			UpdateButtons();
			break;

		default:
			Inherited::MessageReceived(message);
			break;
	}
}


void
RepositoriesView::_AddColumns()
{
	fListView->AddColumn(new BStringColumn(B_TRANSLATE("Enabled"), 70, 50,
		100, B_TRUNCATE_END), kEnabledColumn);
	fListView->AddColumn(new BStringColumn(B_TRANSLATE("Types"), 80, 50,
		140, B_TRUNCATE_END), kTypesColumn);
	fListView->AddColumn(new BStringColumn(B_TRANSLATE("URIs"), 240, 100,
		600, B_TRUNCATE_END), kUrisColumn);
	fListView->AddColumn(new BStringColumn(B_TRANSLATE("Suites"), 130, 60,
		300, B_TRUNCATE_END), kSuitesColumn);
	fListView->AddColumn(new BStringColumn(B_TRANSLATE("Components"), 150,
		60, 300, B_TRUNCATE_END), kComponentsColumn);
	fListView->AddColumn(new BStringColumn(B_TRANSLATE("Signed-By"), 170,
		60, 400, B_TRUNCATE_END), kSignedByColumn);
	fListView->AddColumn(new BStringColumn(B_TRANSLATE("File"), 150, 60,
		300, B_TRUNCATE_END), kFileColumn);
}


void
RepositoriesView::_AddEntry(source_entry* entry)
{
	entry->owned = is_owned_name(entry->name);
	fEntries.AddItem(entry);

	SourceRow* row = new(std::nothrow) SourceRow(entry);
	if (row == NULL)
		return;

	row->SetField(new BStringField(entry->enabled
		? B_TRANSLATE("Yes") : B_TRANSLATE("No")), kEnabledColumn);
	row->SetField(new BStringField(entry->types.String()), kTypesColumn);
	row->SetField(new BStringField(entry->uris.String()), kUrisColumn);
	row->SetField(new BStringField(entry->suites.String()), kSuitesColumn);
	row->SetField(new BStringField(entry->components.String()),
		kComponentsColumn);
	row->SetField(new BStringField(entry->signedBy.String()),
		kSignedByColumn);
	row->SetField(new BStringField(entry->name.String()), kFileColumn);
	fListView->AddRow(row);
}


void
RepositoriesView::_ParseOneLineFile(const BString& path, const BString& name,
	const BString& text)
{
	int32 lineNumber = 0;
	int32 start = 0;
	const int32 length = text.Length();

	while (start <= length) {
		int32 newline = text.FindFirst('\n', start);
		int32 end = newline < 0 ? length : newline;

		BString line;
		text.CopyInto(line, start, end - start);
		lineNumber++;

		source_entry* entry = parse_one_line(path, name, lineNumber, line);
		if (entry != NULL)
			_AddEntry(entry);

		if (newline < 0)
			break;
		start = newline + 1;
	}
}


void
RepositoriesView::_ParseSourcesFile(const BString& path, const BString& name,
	const BString& text)
{
	BObjectList<BString, true> all(32);
	int32 start = 0;
	const int32 length = text.Length();

	while (start <= length) {
		int32 newline = text.FindFirst('\n', start);
		BString line;
		if (newline < 0)
			text.CopyInto(line, start, length - start);
		else
			text.CopyInto(line, start, newline - start);
		all.AddItem(new BString(line));
		if (newline < 0)
			break;
		start = newline + 1;
	}

	int32 at = 0;
	int32 stanza = 0;
	while (at < all.CountItems()) {
		while (at < all.CountItems()) {
			BString probe(*all.ItemAt(at));
			probe.Trim();
			if (probe.Length() > 0)
				break;
			at++;
		}
		if (at >= all.CountItems())
			break;

		stanza++;
		BObjectList<BString, true> stanzaLines(8);
		while (at < all.CountItems()) {
			BString probe(*all.ItemAt(at));
			probe.Trim();
			if (probe.Length() == 0 && stanzaLines.CountItems() > 0)
				break;
			stanzaLines.AddItem(new BString(*all.ItemAt(at)));
			at++;
		}

		source_entry* entry = parse_stanza(path, name, stanza, stanzaLines);
		if (entry != NULL)
			_AddEntry(entry);
	}
}


source_entry*
RepositoriesView::SelectedEntry() const
{
	SourceRow* row = dynamic_cast<SourceRow*>(
		fListView->CurrentSelection());
	return row != NULL ? row->Entry() : NULL;
}


void
RepositoriesView::Reload()
{
	fListView->Clear();
	fEntries.MakeEmpty();

	BString sourcesList("/etc/apt/sources.list");
	BString text;
	if (read_file(sourcesList.String(), &text))
		_ParseOneLineFile(sourcesList, "sources.list", text);

	BDirectory dir("/etc/apt/sources.list.d");
	if (dir.InitCheck() == B_OK) {
		BObjectList<BString, true> paths(16);
		BEntry entry;
		while (dir.GetNextEntry(&entry) == B_OK) {
			BPath path;
			if (entry.GetPath(&path) != B_OK)
				continue;

			struct stat st;
			if (entry.GetStat(&st) != 0 || !S_ISREG(st.st_mode))
				continue;

			BString filePath(path.Path());
			if (has_suffix(filePath, ".sources")
					|| has_suffix(filePath, ".list")) {
				paths.AddItem(new BString(filePath));
			}
		}
		paths.SortItems(compare_paths);

		for (int32 i = 0; i < paths.CountItems(); i++) {
			const BString& filePath = *paths.ItemAt(i);
			text.SetTo("");
			if (!read_file(filePath.String(), &text))
				continue;

			BString base(filePath);
			const int32 slash = base.FindLast('/');
			if (slash >= 0)
				base.Remove(0, slash + 1);

			if (has_suffix(base, ".sources"))
				_ParseSourcesFile(filePath, base, text);
			else
				_ParseOneLineFile(filePath, base, text);
		}
	}

	UpdateButtons();
}


void
RepositoriesView::SetTransactionActive(bool active)
{
	fTransactionActive = active;
	UpdateButtons();
}


void
RepositoriesView::UpdateButtons()
{
	source_entry* entry = SelectedEntry();
	const bool editable = entry != NULL && entry->owned && !entry->oneLine;

	fAddButton->SetEnabled(!fTransactionActive);
	fEditButton->SetEnabled(!fTransactionActive && editable);
	fRemoveButton->SetEnabled(!fTransactionActive && entry != NULL
		&& entry->owned);
	fToggleButton->SetEnabled(!fTransactionActive && entry != NULL);
}
