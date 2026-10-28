/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "CallLog.h"

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <Path.h>
#include <String.h>

#include <stdio.h>
#include <string.h>

#include <FindDirectory.h>


static const uint32 kCallLogMagic = 'PHCL';


CallLog::CallLog()
{
	Load();
}


const CallLogEntry*
CallLog::EntryAt(int32 index) const
{
	return fEntries.ItemAt(index);
}


void
CallLog::Add(const CallLogEntry& entry)
{
	CallLogEntry* stored = new CallLogEntry(entry);
	fEntries.AddItem(stored, 0);
	Save();
}


void
CallLog::Load()
{
	fEntries.MakeEmpty();

	BPath path;
	if (find_directory(B_USER_CONFIG_DIRECTORY, &path) != B_OK)
		return;
	BString fullPath(path.Path());
	fullPath << "/settings/Phone/calllog";

	BFile file(fullPath.String(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;

	BMessage archive;
	if (archive.Unflatten(&file) != B_OK
			|| archive.what != kCallLogMagic)
		return;

	int32 count = 0;
	archive.FindInt32("count", &count);
	for (int32 i = 0; i < count; i++) {
		CallLogEntry* entry = new CallLogEntry;
		char name[32];
		snprintf(name, sizeof(name), "entry_%" B_PRId32, i);
		BMessage item;
		if (archive.FindMessage(name, &item) != B_OK) {
			delete entry;
			continue;
		}
		item.FindString("number", &entry->number);
		BString direction;
		item.FindString("direction", &direction);
		entry->direction = strcmp(direction.String(), "in") == 0
			? "in" : "out";
		BString result;
		item.FindString("result", &result);
		if (result == "missed")
			entry->result = "missed";
		else if (result == "rejected")
			entry->result = "rejected";
		else
			entry->result = "completed";
		int64 stamp = 0;
		item.FindInt64("timestamp", &stamp);
		entry->timestamp = (time_t)stamp;
		fEntries.AddItem(entry);
	}
}


void
CallLog::Save()
{
	BPath path;
	if (find_directory(B_USER_CONFIG_DIRECTORY, &path) != B_OK)
		return;

	BString dirPath(path.Path());
	dirPath << "/settings/Phone";
	create_directory(dirPath.String(), 0755);

	BString fullPath(dirPath.String());
	fullPath << "/calllog";

	BMessage archive(kCallLogMagic);
	archive.AddInt32("count", fEntries.CountItems());
	for (int32 i = 0; i < fEntries.CountItems(); i++) {
		const CallLogEntry* entry = fEntries.ItemAt(i);
		BMessage item;
		item.AddString("number", entry->number);
		item.AddString("direction", entry->direction);
		item.AddString("result", entry->result);
		item.AddInt64("timestamp", (int64)entry->timestamp);
		char name[32];
		snprintf(name, sizeof(name), "entry_%" B_PRId32, i);
		archive.AddMessage(name, &item);
	}

	BFile file(fullPath.String(), B_WRITE_ONLY | B_CREATE_FILE
		| B_ERASE_FILE);
	if (file.InitCheck() != B_OK)
		return;
	archive.Flatten(&file);
}
