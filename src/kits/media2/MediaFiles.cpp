/*
 * Copyright 2025-2026, Dario Casalinuovo. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include <MediaFiles.h>

#include <new>
#include <stdio.h>
#include <string.h>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <List.h>
#include <Message.h>
#include <Path.h>
#include <String.h>


const char BMediaFiles::B_SOUNDS[] = "Sounds";


static const char* kSettingsSubdir = "Media";
static const char* kSettingsFile   = "MediaFiles";


static status_t
get_settings_path(BPath& path, bool createDir)
{
	status_t err = find_directory(B_USER_SETTINGS_DIRECTORY, &path, createDir);
	if (err != B_OK)
		return err;
	if ((err = path.Append(kSettingsSubdir)) != B_OK)
		return err;
	if (createDir) {
		BDirectory dir;
		err = dir.CreateDirectory(path.Path(), &dir);
		if (err != B_OK && err != B_FILE_EXISTS)
			return err;
	}
	return path.Append(kSettingsFile);
}


static status_t
load_store(BMessage& out)
{
	BPath path;
	status_t err = get_settings_path(path, false);
	if (err != B_OK)
		return err;
	BFile file(path.Path(), B_READ_ONLY);
	err = file.InitCheck();
	if (err != B_OK)
		return err;  // missing file is not an error to callers; they treat as empty
	return out.Unflatten(&file);
}


static status_t
save_store(const BMessage& store)
{
	BPath path;
	status_t err = get_settings_path(path, true);
	if (err != B_OK)
		return err;
	BFile file(path.Path(),
		B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	err = file.InitCheck();
	if (err != B_OK)
		return err;
	return store.Flatten(&file);
}


static BString
make_ref_key(const char* item)
{
	BString k("ref:");
	k.Append(item);
	return k;
}


static BString
make_gain_key(const char* item)
{
	BString k("gain:");
	k.Append(item);
	return k;
}


BMediaFiles::BMediaFiles()
	:
	fTypes(),
	fTypeIndex(-1),
	fCurrentType(),
	fItems(),
	fItemIndex(-1)
{
}


BMediaFiles::~BMediaFiles()
{
	_ClearTypes();
	_ClearItems();
}


void
BMediaFiles::_ClearTypes()
{
	for (int32 i = 0; i < fTypes.CountItems(); i++)
		delete (BString*)fTypes.ItemAt(i);
	fTypes.MakeEmpty();
	fTypeIndex = -1;
}


void
BMediaFiles::_ClearItems()
{
	for (int32 i = 0; i < fItems.CountItems(); i++)
		delete (BString*)fItems.ItemAt(i);
	fItems.MakeEmpty();
	fItemIndex = -1;
	fCurrentType = "";
}


status_t
BMediaFiles::RewindTypes()
{
	_ClearTypes();

	BMessage store;
	if (load_store(store) != B_OK) {
		fTypeIndex = 0;
		return B_OK;  // empty store -> no types
	}

	char* name;
	type_code type;
	int32 count;
	for (int32 i = 0;
			store.GetInfo(B_ANY_TYPE, i, &name, &type, &count) == B_OK; i++) {
		if (type == B_MESSAGE_TYPE)
			fTypes.AddItem(new BString(name));
	}
	fTypeIndex = 0;
	return B_OK;
}


status_t
BMediaFiles::GetNextType(BString* _type)
{
	if (_type == NULL)
		return B_BAD_VALUE;
	if (fTypeIndex < 0)
		return B_BAD_INDEX;
	if (fTypeIndex >= fTypes.CountItems())
		return B_BAD_INDEX;
	*_type = *(BString*)fTypes.ItemAt(fTypeIndex);
	fTypeIndex++;
	return B_OK;
}


status_t
BMediaFiles::RewindRefs(const char* type)
{
	if (type == NULL)
		return B_BAD_VALUE;
	_ClearItems();
	fCurrentType = type;

	BMessage store;
	if (load_store(store) != B_OK) {
		fItemIndex = 0;
		return B_OK;
	}
	BMessage typeMsg;
	if (store.FindMessage(type, &typeMsg) != B_OK) {
		fItemIndex = 0;
		return B_OK;
	}

	char* name;
	type_code t;
	int32 count;
	for (int32 i = 0;
			typeMsg.GetInfo(B_ANY_TYPE, i, &name, &t, &count) == B_OK; i++) {
		// Item names appear under both "ref:<name>" and optionally "gain:<name>".
		// Enumerate via the "ref:" prefix so each item is reported once.
		if (strncmp(name, "ref:", 4) == 0)
			fItems.AddItem(new BString(name + 4));
	}
	fItemIndex = 0;
	return B_OK;
}


status_t
BMediaFiles::GetNextRef(BString* _name, entry_ref* _ref)
{
	if (_name == NULL)
		return B_BAD_VALUE;
	if (fItemIndex < 0 || fItemIndex >= fItems.CountItems())
		return B_BAD_INDEX;

	BString* item = (BString*)fItems.ItemAt(fItemIndex);
	*_name = *item;
	if (_ref != NULL) {
		// Best-effort: missing ref is silently zeroed.
		if (GetRefFor(fCurrentType.String(), item->String(), _ref) != B_OK)
			*_ref = entry_ref();
	}
	fItemIndex++;
	return B_OK;
}


status_t
BMediaFiles::GetRefFor(const char* type, const char* item, entry_ref* _ref)
{
	if (type == NULL || item == NULL || _ref == NULL)
		return B_BAD_VALUE;

	BMessage store;
	status_t err = load_store(store);
	if (err != B_OK)
		return B_ENTRY_NOT_FOUND;
	BMessage typeMsg;
	if (store.FindMessage(type, &typeMsg) != B_OK)
		return B_ENTRY_NOT_FOUND;
	return typeMsg.FindRef(make_ref_key(item).String(), _ref);
}


status_t
BMediaFiles::SetRefFor(const char* type, const char* item, const entry_ref& ref)
{
	if (type == NULL || item == NULL)
		return B_BAD_VALUE;

	BMessage store;
	load_store(store);  // ignore error; treat as empty

	BMessage typeMsg;
	store.FindMessage(type, &typeMsg);  // ignore error

	BString key = make_ref_key(item);
	typeMsg.RemoveName(key.String());
	status_t err = typeMsg.AddRef(key.String(), &ref);
	if (err != B_OK)
		return err;

	store.RemoveName(type);
	if ((err = store.AddMessage(type, &typeMsg)) != B_OK)
		return err;
	return save_store(store);
}


status_t
BMediaFiles::GetAudioGainFor(const char* type, const char* item, float* _gain)
{
	if (type == NULL || item == NULL || _gain == NULL)
		return B_BAD_VALUE;

	BMessage store;
	if (load_store(store) != B_OK)
		return B_ENTRY_NOT_FOUND;
	BMessage typeMsg;
	if (store.FindMessage(type, &typeMsg) != B_OK)
		return B_ENTRY_NOT_FOUND;
	return typeMsg.FindFloat(make_gain_key(item).String(), _gain);
}


status_t
BMediaFiles::SetAudioGainFor(const char* type, const char* item, float gain)
{
	if (type == NULL || item == NULL)
		return B_BAD_VALUE;

	BMessage store;
	load_store(store);

	BMessage typeMsg;
	store.FindMessage(type, &typeMsg);

	BString key = make_gain_key(item);
	typeMsg.RemoveName(key.String());
	status_t err = typeMsg.AddFloat(key.String(), gain);
	if (err != B_OK)
		return err;

	store.RemoveName(type);
	if ((err = store.AddMessage(type, &typeMsg)) != B_OK)
		return err;
	return save_store(store);
}


status_t
BMediaFiles::RemoveRefFor(const char* type, const char* item,
	const entry_ref& /*ref*/)
{
	if (type == NULL || item == NULL)
		return B_BAD_VALUE;

	BMessage store;
	if (load_store(store) != B_OK)
		return B_OK;  // nothing to remove
	BMessage typeMsg;
	if (store.FindMessage(type, &typeMsg) != B_OK)
		return B_OK;
	typeMsg.RemoveName(make_ref_key(item).String());

	store.RemoveName(type);
	status_t err = store.AddMessage(type, &typeMsg);
	if (err != B_OK)
		return err;
	return save_store(store);
}


status_t
BMediaFiles::RemoveItem(const char* type, const char* item)
{
	if (type == NULL || item == NULL)
		return B_BAD_VALUE;

	BMessage store;
	if (load_store(store) != B_OK)
		return B_OK;
	BMessage typeMsg;
	if (store.FindMessage(type, &typeMsg) != B_OK)
		return B_OK;
	typeMsg.RemoveName(make_ref_key(item).String());
	typeMsg.RemoveName(make_gain_key(item).String());

	store.RemoveName(type);
	status_t err = store.AddMessage(type, &typeMsg);
	if (err != B_OK)
		return err;
	return save_store(store);
}


// FBC stubs
status_t BMediaFiles::_Reserved_MediaFiles_0(void*, ...) { return B_ERROR; }
status_t BMediaFiles::_Reserved_MediaFiles_1(void*, ...) { return B_ERROR; }
status_t BMediaFiles::_Reserved_MediaFiles_2(void*, ...) { return B_ERROR; }
status_t BMediaFiles::_Reserved_MediaFiles_3(void*, ...) { return B_ERROR; }
status_t BMediaFiles::_Reserved_MediaFiles_4(void*, ...) { return B_ERROR; }
status_t BMediaFiles::_Reserved_MediaFiles_5(void*, ...) { return B_ERROR; }
status_t BMediaFiles::_Reserved_MediaFiles_6(void*, ...) { return B_ERROR; }
status_t BMediaFiles::_Reserved_MediaFiles_7(void*, ...) { return B_ERROR; }
