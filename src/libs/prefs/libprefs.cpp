/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <libprefs.h>

#include <string.h>

#include <new>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Path.h>
#include <String.h>


static const char* kLibPrefsDirectory = "libprefs";
static const char* kSetFileName = "_set";
static const char* kLibPrefsVersion = "V\\OS libprefs 1.2.4";


struct _PREFHandle {
	BString name;
};

struct _PREFData {
	_PREFHandle*	handle;
	BString			set;
	bool			userLevel;
	BMessage		message;
};

struct _PREFTransaction {
};


static status_t
root_directory(bool userLevel, BPath& path)
{
	return find_directory(userLevel ? B_USER_SETTINGS_DIRECTORY
		: B_SYSTEM_SETTINGS_DIRECTORY, &path, true);
}


static status_t
app_path(const char* app, bool userLevel, BPath& path)
{
	status_t status = root_directory(userLevel, path);
	if (status != B_OK)
		return status;
	status = path.Append(kLibPrefsDirectory);
	if (status == B_OK)
		status = path.Append(app);
	return status;
}


static status_t
set_directory(const _PREFData* data, BPath& path)
{
	status_t status = app_path(data->handle->name.String(), data->userLevel,
		path);
	if (status != B_OK)
		return status;
	return path.Append(data->set.String());
}


static status_t
set_file_path(const _PREFData* data, BPath& path)
{
	status_t status = set_directory(data, path);
	if (status == B_OK)
		status = path.Append(kSetFileName);
	return status;
}


static bool
valid_set_name(const char* set)
{
	if (set == NULL || set[0] == '\0' || set[0] == '/')
		return false;
	BString name(set);
	if (name.FindFirst("//") >= 0 || name.FindFirst("..") >= 0)
		return false;
	return name[name.Length() - 1] != '/';
}


static status_t
remove_tree(BDirectory& directory)
{
	BEntry entry;
	directory.Rewind();
	while (directory.GetNextEntry(&entry) == B_OK) {
		if (entry.IsDirectory()) {
			BDirectory child(&entry);
			remove_tree(child);
		}
		status_t status = entry.Remove();
		if (status != B_OK)
			return status;
	}
	return B_OK;
}


static status_t
list_directory(const BPath& path, uint32* cookie, char* name, size_t size,
	bool directoriesOnly)
{
	BDirectory directory(path.Path());
	status_t status = directory.InitCheck();
	if (status != B_OK)
		return PREF_NOT_FOUND;

	BEntry entry;
	char entryName[B_FILE_NAME_LENGTH];
	uint32 index = 0;
	while (directory.GetNextEntry(&entry) == B_OK) {
		if (directoriesOnly && !entry.IsDirectory())
			continue;
		if (index++ < *cookie)
			continue;
		entry.GetName(entryName);
		if (strlen(entryName) >= size)
			return PREF_TOO_LARGE;
		strcpy(name, entryName);
		*cookie = index;
		return B_OK;
	}
	return B_ENTRY_NOT_FOUND;
}


const char*
PREFVersion(uint32* outVersion, uint32* outMinRequired)
{
	if (outVersion != NULL)
		*outVersion = PREF_CUR_VERSION;
	if (outMinRequired != NULL)
		*outMinRequired = PREF_MIN_VERSION;
	return kLibPrefsVersion;
}


status_t
PREFInit(const char* application, PREFHandle* outHandle)
{
	if (application == NULL || application[0] == '\0'
		|| strchr(application, '/') != NULL || outHandle == NULL)
		return B_BAD_VALUE;

	_PREFHandle* handle = new(std::nothrow) _PREFHandle;
	if (handle == NULL)
		return B_NO_MEMORY;
	handle->name = application;
	*outHandle = handle;
	return B_OK;
}


status_t
PREFRemoveApp(PREFHandle handle)
{
	if (handle == NULL)
		return PREF_NOT_INITED;

	for (int i = 0; i < 2; i++) {
		BPath path;
		if (app_path(handle->name.String(), i == 0, path) != B_OK)
			continue;
		BEntry entry(path.Path());
		if (!entry.Exists())
			continue;
		BDirectory directory(&entry);
		if (remove_tree(directory) != B_OK || entry.Remove() != B_OK)
			return PREF_REMOVE_FAILED;
	}
	delete handle;
	return B_OK;
}


status_t
PREFShutdown(PREFHandle handle)
{
	if (handle == NULL)
		return PREF_NOT_INITED;
	delete handle;
	return B_OK;
}


status_t
PREFReloadSet(PREFData data)
{
	if (data == NULL)
		return B_BAD_VALUE;

	BPath path;
	status_t status = set_file_path(data, path);
	if (status != B_OK)
		return status;

	data->message.MakeEmpty();
	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return B_OK;
	return data->message.Unflatten(&file);
}


status_t
PREFLoadSet(PREFHandle handle, const char* set, bool userLevel,
	PREFData* outData)
{
	if (handle == NULL)
		return PREF_NOT_INITED;
	if (!valid_set_name(set) || outData == NULL)
		return B_BAD_VALUE;

	_PREFData* data = new(std::nothrow) _PREFData;
	if (data == NULL)
		return B_NO_MEMORY;
	data->handle = handle;
	data->set = set;
	data->userLevel = userLevel;

	status_t status = PREFReloadSet(data);
	if (status != B_OK) {
		delete data;
		return status;
	}
	*outData = data;
	return B_OK;
}


status_t
PREFSaveSet(PREFData data)
{
	if (data == NULL)
		return B_BAD_VALUE;

	BPath directoryPath;
	status_t status = set_directory(data, directoryPath);
	if (status != B_OK)
		return status;
	status = create_directory(directoryPath.Path(), 0755);
	if (status != B_OK)
		return status;

	BPath path;
	status = set_file_path(data, path);
	if (status != B_OK)
		return status;

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status = file.InitCheck();
	if (status != B_OK)
		return status;
	return data->message.Flatten(&file);
}


status_t
PREFDisposeSet(PREFData* data)
{
	if (data == NULL || *data == NULL)
		return B_BAD_VALUE;
	delete *data;
	*data = NULL;
	return B_OK;
}


status_t
PREFRemoveSet(PREFData data)
{
	if (data == NULL)
		return B_BAD_VALUE;

	BPath path;
	status_t status = set_file_path(data, path);
	if (status != B_OK)
		return status;

	BEntry entry(path.Path());
	if (entry.Exists() && entry.Remove() != B_OK)
		return PREF_REMOVE_FAILED;

	if (set_directory(data, path) == B_OK) {
		BEntry directory(path.Path());
		directory.Remove();
	}
	delete data;
	return B_OK;
}


status_t
PREFSetData(PREFData data, const char* setting, const void* buffer,
	size_t size, uint32 type)
{
	if (data == NULL || setting == NULL)
		return B_BAD_VALUE;

	data->message.RemoveName(setting);
	return data->message.AddData(setting, type, buffer, size, false);
}


status_t
PREFDelete(PREFData data, const char* setting)
{
	if (data == NULL || setting == NULL)
		return B_BAD_VALUE;
	return data->message.RemoveName(setting) == B_OK ? B_OK : PREF_NOT_FOUND;
}


status_t
PREFGetData(PREFData data, const char* setting, const void** outData,
	ssize_t* outSize, uint32* outType)
{
	if (data == NULL || setting == NULL)
		return B_BAD_VALUE;

	type_code type;
	if (data->message.GetInfo(setting, &type) != B_OK)
		return PREF_NOT_FOUND;

	const void* found;
	ssize_t size;
	if (data->message.FindData(setting, type, &found, &size) != B_OK)
		return PREF_NOT_FOUND;

	if (outData != NULL)
		*outData = found;
	if (outSize != NULL)
		*outSize = size;
	if (outType != NULL)
		*outType = type;
	return B_OK;
}


BMessage*
PREFGetMessage(PREFData data)
{
	return data != NULL ? &data->message : NULL;
}


status_t
PREFListApplications(PREFHandle handle, bool userLevel, uint32* cookie,
	char* appName, size_t size)
{
	if (handle == NULL)
		return PREF_NOT_INITED;
	if (cookie == NULL || appName == NULL)
		return B_BAD_VALUE;

	BPath path;
	status_t status = root_directory(userLevel, path);
	if (status == B_OK)
		status = path.Append(kLibPrefsDirectory);
	if (status != B_OK)
		return status;
	return list_directory(path, cookie, appName, size, true);
}


status_t
PREFDisposeApplicationCookie(PREFHandle handle, uint32 cookie)
{
	return handle != NULL ? B_OK : PREF_NOT_INITED;
}


status_t
PREFListSets(PREFHandle handle, bool userLevel, uint32* cookie, char* setName,
	size_t size)
{
	if (handle == NULL)
		return PREF_NOT_INITED;
	if (cookie == NULL || setName == NULL)
		return B_BAD_VALUE;

	BPath path;
	status_t status = app_path(handle->name.String(), userLevel, path);
	if (status != B_OK)
		return status;
	return list_directory(path, cookie, setName, size, true);
}


status_t
PREFDisposeSetCookie(PREFHandle handle, uint32 cookie)
{
	return handle != NULL ? B_OK : PREF_NOT_INITED;
}


status_t
PREFListData(PREFData data, uint32* cookie, char* dataName, size_t size,
	uint32* dataType)
{
	if (data == NULL)
		return B_BAD_VALUE;
	if (cookie == NULL || dataName == NULL)
		return B_BAD_VALUE;

	char* name;
	type_code type;
	status_t status = data->message.GetInfo(B_ANY_TYPE, (int32)*cookie, &name,
		&type);
	if (status != B_OK)
		return B_ENTRY_NOT_FOUND;
	if (strlen(name) >= size)
		return PREF_TOO_LARGE;

	strcpy(dataName, name);
	if (dataType != NULL)
		*dataType = type;
	(*cookie)++;
	return B_OK;
}


status_t
PREFDisposeDataCookie(PREFData data, uint32 cookie)
{
	return data != NULL ? B_OK : B_BAD_VALUE;
}


status_t
PREFBeginTransaction(PREFData data, PREFTransaction* transaction)
{
	if (data == NULL || transaction == NULL)
		return B_BAD_VALUE;
	if (*transaction != NULL)
		return B_OK;

	*transaction = new(std::nothrow) _PREFTransaction;
	return *transaction != NULL ? B_OK : B_NO_MEMORY;
}


status_t
PREFEndTransaction(PREFTransaction transaction)
{
	delete transaction;
	return B_OK;
}


status_t
PREFListen(PREFData data, void (*callback)(PREFData data, void* cookie),
	void* cookie)
{
	return callback == NULL ? B_OK : PREF_NOT_AVAILABLE;
}
