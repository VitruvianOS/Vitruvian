/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "RepositoriesWindow.h"

#include <stdio.h>
#include <string.h>

#include <Catalog.h>
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <Directory.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <Path.h>
#include <String.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "RepositoriesWindow"


static const char* kSourcesList = "/etc/apt/sources.list";
static const char* kSourcesDirectory = "/etc/apt/sources.list.d";


static bool
has_suffix(const char* name, const char* suffix)
{
	size_t length = strlen(name);
	size_t suffixLength = strlen(suffix);
	return length > suffixLength
		&& strcmp(name + length - suffixLength, suffix) == 0;
}


RepositoriesWindow::RepositoriesWindow()
	:
	Inherited(BRect(0, 0, 720, 320), B_TRANSLATE("Repositories"),
		B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS)
{
	fListView = new BColumnListView("repositories", 0, B_FANCY_BORDER);
	fListView->AddColumn(new BStringColumn(B_TRANSLATE("File"), 160, 60, 400,
		B_TRUNCATE_MIDDLE), 0);
	fListView->AddColumn(new BStringColumn(B_TRANSLATE("Type"), 60, 40, 120,
		B_TRUNCATE_END), 1);
	fListView->AddColumn(new BStringColumn(B_TRANSLATE("URI"), 240, 60, 600,
		B_TRUNCATE_MIDDLE), 2);
	fListView->AddColumn(new BStringColumn(B_TRANSLATE("Suite"), 100, 40, 300,
		B_TRUNCATE_END), 3);
	fListView->AddColumn(new BStringColumn(B_TRANSLATE("Components"), 120, 40,
		300, B_TRUNCATE_END), 4);
	fListView->AddColumn(new BStringColumn(B_TRANSLATE("Enabled"), 60, 40, 100,
		B_TRUNCATE_END), 5);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(fListView);

	_Load();
	CenterOnScreen();
}


bool
RepositoriesWindow::QuitRequested()
{
	return true;
}


void
RepositoriesWindow::_Load()
{
	_LoadOneLine(kSourcesList);

	BDirectory directory(kSourcesDirectory);
	BEntry entry;
	while (directory.GetNextEntry(&entry) == B_OK) {
		BPath path;
		if (entry.GetPath(&path) != B_OK)
			continue;
		if (has_suffix(path.Leaf(), ".sources"))
			_LoadDeb822(path.Path());
		else if (has_suffix(path.Leaf(), ".list"))
			_LoadOneLine(path.Path());
	}
}


void
RepositoriesWindow::_LoadDeb822(const char* path)
{
	FILE* file = fopen(path, "r");
	if (file == NULL)
		return;

	BString type, uris, suites, components;
	bool enabled = true;
	bool any = false;
	const char* leaf = strrchr(path, '/') != NULL
		? strrchr(path, '/') + 1 : path;

	char line[1024];
	for (;;) {
		bool more = fgets(line, sizeof(line), file) != NULL;
		BString text(more ? line : "");
		text.Trim();

		if (!more || text.Length() == 0) {
			if (any)
				_AddRow(leaf, type, uris, suites, components, enabled);
			type = uris = suites = components = "";
			enabled = true;
			any = false;
			if (!more)
				break;
			continue;
		}

		if (text[0] == '#' || text[0] == ' ' || text[0] == '\t')
			continue;

		int32 colon = text.FindFirst(':');
		if (colon < 0)
			continue;

		BString key(text.String(), colon);
		BString value(text.String() + colon + 1);
		value.Trim();
		any = true;

		if (key.ICompare("Types") == 0)
			type = value;
		else if (key.ICompare("URIs") == 0)
			uris = value;
		else if (key.ICompare("Suites") == 0)
			suites = value;
		else if (key.ICompare("Components") == 0)
			components = value;
		else if (key.ICompare("Enabled") == 0)
			enabled = value.ICompare("no") != 0 && value.ICompare("false") != 0;
	}

	fclose(file);
}


void
RepositoriesWindow::_LoadOneLine(const char* path)
{
	FILE* file = fopen(path, "r");
	if (file == NULL)
		return;

	const char* leaf = strrchr(path, '/') != NULL
		? strrchr(path, '/') + 1 : path;

	char line[1024];
	while (fgets(line, sizeof(line), file) != NULL) {
		BString text(line);
		text.Trim();

		bool enabled = true;
		if (text.Length() > 0 && text[0] == '#') {
			// a commented-out entry still starts with the type
			text.Remove(0, 1);
			text.Trim();
			if (!text.StartsWith("deb ") && !text.StartsWith("deb-src "))
				continue;
			enabled = false;
		}

		if (!text.StartsWith("deb ") && !text.StartsWith("deb-src "))
			continue;

		BString fields[16];
		int32 count = 0;
		int32 position = 0;
		while (count < 16 && position < text.Length()) {
			int32 end = text.FindFirst(' ', position);
			if (end < 0)
				end = text.Length();
			if (end > position) {
				text.CopyInto(fields[count], position, end - position);
				// skip [options]
				if (fields[count][0] != '[')
					count++;
			}
			position = end + 1;
		}
		if (count < 3)
			continue;

		BString components;
		for (int32 i = 3; i < count; i++) {
			if (i > 3)
				components << " ";
			components << fields[i];
		}
		_AddRow(leaf, fields[0], fields[1], fields[2], components, enabled);
	}

	fclose(file);
}


void
RepositoriesWindow::_AddRow(const char* file, const char* type,
	const char* uris, const char* suites, const char* components,
	bool enabled)
{
	BRow* row = new BRow();
	row->SetField(new BStringField(file), 0);
	row->SetField(new BStringField(type), 1);
	row->SetField(new BStringField(uris), 2);
	row->SetField(new BStringField(suites), 3);
	row->SetField(new BStringField(components), 4);
	row->SetField(new BStringField(enabled ? B_TRANSLATE("yes")
		: B_TRANSLATE("no")), 5);
	fListView->AddRow(row);
}
