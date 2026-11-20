/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "StoreCatalog.h"

#include <File.h>
#include <Message.h>

#include <Json.h>

#include <stdlib.h>
#include <strings.h>


//	#pragma mark - StoreEntry


StoreEntry::StoreEntry()
	:
	fRequires(2),
	fPackages(4)
{
}


StoreEntry::~StoreEntry()
{
}


bool
StoreEntry::IsValid() const
{
	if (fId.IsEmpty() || fName.IsEmpty())
		return false;

	// Only the three catalog kinds exist; anything else is rejected so
	// the page can never render a library or dev package.
	return fKind == "app" || fKind == "component" || fKind == "data";
}


static int32
_kindRank(const BString& kind)
{
	// Components first (installing an app may pull one), then apps,
	// then data.
	if (kind == "component")
		return 0;
	if (kind == "app")
		return 1;
	return 2;
}


int32
StoreEntry::Compare(const StoreEntry* a, const StoreEntry* b)
{
	int32 rankA = _kindRank(a->fKind);
	int32 rankB = _kindRank(b->fKind);
	if (rankA != rankB)
		return rankA < rankB ? -1 : 1;

	int32 result = strcasecmp(a->fName.String(), b->fName.String());
	if (result != 0)
		return result;
	return strcmp(a->fId.String(), b->fId.String());
}


//	#pragma mark - StoreCatalog


StoreCatalog::StoreCatalog()
	:
	fEntries(32)
{
}


StoreCatalog::~StoreCatalog()
{
	// fEntries owns the items.
}


static void
_readStrings(const BMessage& array, BObjectList<BString, true>& target)
{
	BString name;
	BString value;
	for (int32 i = 0; ; i++) {
		name.SetToFormat("%" B_PRId32, i);
		if (array.FindString(name.String(), &value) != B_OK)
			break;
		if (!value.IsEmpty())
			target.AddItem(new BString(value));
	}
}


status_t
StoreCatalog::LoadFrom(const char* path)
{
	BFile file(path, B_READ_ONLY);
	status_t status = file.InitCheck();
	if (status != B_OK)
		return status;

	off_t size = 0;
	if (file.GetSize(&size) != B_OK || size <= 0 || size > 4 * 1024 * 1024)
		return B_BAD_DATA;

	BString data;
	char* buffer = data.LockBuffer(size + 1);
	if (buffer == NULL)
		return B_NO_MEMORY;
	ssize_t read = file.Read(buffer, size);
	buffer[read > 0 ? read : 0] = '\0';
	data.UnlockBuffer(read > 0 ? read : 0);
	if (read <= 0)
		return B_IO_ERROR;

	return LoadFromData(data.String());
}


status_t
StoreCatalog::LoadFromData(const char* json)
{
	BMessage root;
	status_t status = BJson::Parse(json, root);
	if (status != B_OK)
		return status;

	BMessage entries;
	if (root.FindMessage("entries", &entries) != B_OK)
		return B_BAD_DATA;

	fEntries.MakeEmpty();

	BString name;
	BMessage object;
	for (int32 i = 0; ; i++) {
		name.SetToFormat("%" B_PRId32, i);
		if (entries.FindMessage(name.String(), &object) != B_OK)
			break;

		StoreEntry* item = new StoreEntry();
		object.FindString("id", &item->fId);
		object.FindString("kind", &item->fKind);
		object.FindString("name", &item->fName);
		object.FindString("summary", &item->fSummary);
		object.FindString("category", &item->fCategory);
		object.FindString("icon", &item->fIcon);

		BMessage list;
		if (object.FindMessage("packages", &list) == B_OK)
			_readStrings(list, item->fPackages);
		if (object.FindMessage("requires", &list) == B_OK)
			_readStrings(list, item->fRequires);

		if (item->IsValid())
			fEntries.AddItem(item);
		else
			delete item;
	}

	fEntries.SortItems((int32 (*)(const StoreEntry*, const StoreEntry*))
		&StoreEntry::Compare);
	return B_OK;
}


StoreEntry*
StoreCatalog::FindById(const char* id) const
{
	if (id == NULL)
		return NULL;

	for (int32 i = 0; i < fEntries.CountItems(); i++) {
		StoreEntry* entry = fEntries.ItemAt(i);
		if (entry->fId == id)
			return entry;
	}
	return NULL;
}
