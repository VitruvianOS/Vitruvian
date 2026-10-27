/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <libprefs.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <new>
#include <vector>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Handler.h>
#include <Locker.h>
#include <Autolock.h>
#include <Looper.h>
#include <Message.h>
#include <MessageFilter.h>
#include <Messenger.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <String.h>


static const char* kLibPrefsDirectory = "libprefs";
static const char* kSetFileName = "_set";
static const char* kLibPrefsVersion = "V\\OS libprefs 1.2.4";


static const bigtime_t kTransactionTimeout = 10000000LL;


class _PREFListener;

struct _PREFHandle {
	BString	name;
	int32	refs;
};

struct _PREFData {
	_PREFData()
		:
		messageLock("libprefs data"),
		transactionLock("libprefs transaction"),
		transaction(NULL),
		listener(NULL),
		callback(NULL),
		cookie(NULL)
	{
	}

	_PREFHandle*	handle;
	BString			set;
	bool			userLevel;
	int32			refs;
	BMessage		message;
	BLocker			messageLock;

	BLocker			transactionLock;
	struct _PREFTransaction* transaction;

	_PREFListener*	listener;
	void			(*callback)(PREFData data, void* cookie);
	void*			cookie;
};

struct _PREFTransaction {
	_PREFData*	data;
	int32		depth;
	bool		dirty;
};


// Handles and sets are shared: the same name gives the same object.
static BLocker sRegistryLock("libprefs registry");
static std::vector<_PREFHandle*> sHandles;
static std::vector<_PREFData*> sSets;

static BLooper* sListenLooper = NULL;


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


class _PREFListener : public BHandler {
public:
	_PREFListener(_PREFData* data)
		:
		BHandler("libprefs listener"),
		fData(data)
	{
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what != B_NODE_MONITOR || fData->callback == NULL) {
			BHandler::MessageReceived(message);
			return;
		}

		int32 opcode;
		const char* name;
		if (message->FindInt32("opcode", &opcode) != B_OK
			|| message->FindString("name", &name) != B_OK
			|| strcmp(name, kSetFileName) != 0)
			return;

		switch (opcode) {
			case B_ENTRY_CREATED:
			case B_ENTRY_REMOVED:
			case B_ENTRY_MOVED:
				fData->callback(fData, fData->cookie);
				break;
		}
	}

private:
	_PREFData*	fData;
};


// Called with the listen looper locked.
static void
stop_listening(_PREFData* data)
{
	if (data->listener == NULL)
		return;

	stop_watching(BMessenger(data->listener, sListenLooper));
	sListenLooper->RemoveHandler(data->listener);
	delete data->listener;
	data->listener = NULL;
	data->callback = NULL;
	data->cookie = NULL;
}


static BLooper*
listen_looper()
{
	BAutolock _(sRegistryLock);
	if (sListenLooper == NULL) {
		sListenLooper = new(std::nothrow) BLooper("libprefs listen");
		if (sListenLooper != NULL)
			sListenLooper->Run();
	}
	return sListenLooper;
}


// Writes the set through a temporary file so readers never see a partial one.
static status_t
write_set(_PREFData* data)
{
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

	BString temporary;
	temporary.SetToFormat("%s.tmp.%d.%d", path.Path(), (int)getpid(),
		(int)find_thread(NULL));

	{
		BFile file(temporary.String(),
			B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
		status = file.InitCheck();
		if (status != B_OK)
			return status;

		BAutolock locker(data->messageLock);
		status = data->message.Flatten(&file);
		if (status == B_OK)
			status = file.Sync();
	}

	if (status == B_OK && rename(temporary.String(), path.Path()) != 0)
		status = errno;
	if (status != B_OK)
		unlink(temporary.String());
	return status;
}


status_t
PREFInit(const char* application, PREFHandle* outHandle)
{
	if (application == NULL || application[0] == '\0'
		|| strchr(application, '/') != NULL || outHandle == NULL)
		return B_BAD_VALUE;

	BAutolock locker(sRegistryLock);
	for (size_t i = 0; i < sHandles.size(); i++) {
		if (sHandles[i]->name == application) {
			sHandles[i]->refs++;
			*outHandle = sHandles[i];
			return B_OK;
		}
	}

	_PREFHandle* handle = new(std::nothrow) _PREFHandle;
	if (handle == NULL)
		return B_NO_MEMORY;
	handle->name = application;
	handle->refs = 1;
	sHandles.push_back(handle);
	*outHandle = handle;
	return B_OK;
}


static void
forget_handle(_PREFHandle* handle)
{
	for (size_t i = 0; i < sHandles.size(); i++) {
		if (sHandles[i] == handle) {
			sHandles.erase(sHandles.begin() + i);
			break;
		}
	}
	delete handle;
}


status_t
PREFRemoveApp(PREFHandle handle)
{
	if (handle == NULL)
		return PREF_NOT_INITED;

	BAutolock locker(sRegistryLock);
	if (handle->refs > 1)
		return PREF_REMOVE_FAILED;

	for (size_t i = 0; i < sSets.size(); i++) {
		if (sSets[i]->handle == handle)
			return PREF_REMOVE_FAILED;
	}

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
	forget_handle(handle);
	return B_OK;
}


status_t
PREFShutdown(PREFHandle handle)
{
	if (handle == NULL)
		return PREF_NOT_INITED;

	BAutolock locker(sRegistryLock);
	if (--handle->refs <= 0)
		forget_handle(handle);
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

	BAutolock locker(data->messageLock);
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

	BAutolock locker(sRegistryLock);
	for (size_t i = 0; i < sSets.size(); i++) {
		_PREFData* found = sSets[i];
		if (found->handle == handle && found->userLevel == userLevel
			&& found->set == set) {
			found->refs++;
			*outData = found;
			return B_OK;
		}
	}

	_PREFData* data = new(std::nothrow) _PREFData;
	if (data == NULL)
		return B_NO_MEMORY;
	data->handle = handle;
	data->set = set;
	data->userLevel = userLevel;
	data->refs = 1;

	status_t status = PREFReloadSet(data);
	if (status != B_OK) {
		delete data;
		return status;
	}
	sSets.push_back(data);
	*outData = data;
	return B_OK;
}


status_t
PREFSaveSet(PREFData data)
{
	if (data == NULL)
		return B_BAD_VALUE;

	// inside our own transaction the write is deferred to the commit
	if (data->transaction != NULL && data->transactionLock.IsLocked()) {
		data->transaction->dirty = true;
		return B_OK;
	}
	return write_set(data);
}


// Unlink from the registry; the caller holds sRegistryLock.
static void
unregister_set(_PREFData* data)
{
	for (size_t i = 0; i < sSets.size(); i++) {
		if (sSets[i] == data) {
			sSets.erase(sSets.begin() + i);
			break;
		}
	}
}


// Called without sRegistryLock: the listen looper may be inside a callback
// that takes it.
static void
destroy_set(_PREFData* data)
{
	if (sListenLooper != NULL && sListenLooper->Lock()) {
		stop_listening(data);
		sListenLooper->Unlock();
	}
	delete data;
}


status_t
PREFDisposeSet(PREFData* data)
{
	if (data == NULL || *data == NULL)
		return B_BAD_VALUE;

	sRegistryLock.Lock();
	if (--(*data)->refs > 0) {
		sRegistryLock.Unlock();
		return B_OK;
	}
	unregister_set(*data);
	sRegistryLock.Unlock();

	destroy_set(*data);
	*data = NULL;
	return B_OK;
}


status_t
PREFRemoveSet(PREFData data)
{
	if (data == NULL)
		return B_BAD_VALUE;

	sRegistryLock.Lock();
	if (data->refs > 1) {
		sRegistryLock.Unlock();
		return PREF_REMOVE_FAILED;
	}

	BPath path;
	status_t status = set_file_path(data, path);
	if (status != B_OK) {
		sRegistryLock.Unlock();
		return status;
	}

	BEntry entry(path.Path());
	if (entry.Exists() && entry.Remove() != B_OK) {
		sRegistryLock.Unlock();
		return PREF_REMOVE_FAILED;
	}

	if (set_directory(data, path) == B_OK) {
		BEntry directory(path.Path());
		directory.Remove();
	}
	unregister_set(data);
	sRegistryLock.Unlock();

	destroy_set(data);
	return B_OK;
}


status_t
PREFSetData(PREFData data, const char* setting, const void* buffer,
	size_t size, uint32 type)
{
	if (data == NULL || setting == NULL)
		return B_BAD_VALUE;

	BAutolock locker(data->messageLock);
	data->message.RemoveName(setting);
	return data->message.AddData(setting, type, buffer, size, false);
}


status_t
PREFDelete(PREFData data, const char* setting)
{
	if (data == NULL || setting == NULL)
		return B_BAD_VALUE;

	BAutolock locker(data->messageLock);
	return data->message.RemoveName(setting) == B_OK ? B_OK : PREF_NOT_FOUND;
}


status_t
PREFGetData(PREFData data, const char* setting, const void** outData,
	ssize_t* outSize, uint32* outType)
{
	if (data == NULL || setting == NULL)
		return B_BAD_VALUE;

	BAutolock locker(data->messageLock);
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

	// nested: the caller passed its current transaction
	if (*transaction != NULL) {
		(*transaction)->depth++;
		return B_OK;
	}

	status_t status = data->transactionLock.LockWithTimeout(
		kTransactionTimeout);
	if (status != B_OK)
		return PREF_TIME_OUT;

	_PREFTransaction* created = new(std::nothrow) _PREFTransaction;
	if (created == NULL) {
		data->transactionLock.Unlock();
		return B_NO_MEMORY;
	}
	created->data = data;
	created->depth = 1;
	created->dirty = false;
	data->transaction = created;
	*transaction = created;
	return B_OK;
}


status_t
PREFEndTransaction(PREFTransaction transaction)
{
	if (transaction == NULL)
		return B_BAD_VALUE;
	if (--transaction->depth > 0)
		return B_OK;

	// commit everything the transaction buffered in one atomic write
	_PREFData* data = transaction->data;
	status_t status = B_OK;
	if (transaction->dirty)
		status = write_set(data);

	data->transaction = NULL;
	data->transactionLock.Unlock();
	delete transaction;
	return status;
}


status_t
PREFListen(PREFData data, void (*callback)(PREFData data, void* cookie),
	void* cookie)
{
	if (data == NULL)
		return B_BAD_VALUE;

	if (callback == NULL) {
		if (sListenLooper != NULL && sListenLooper->Lock()) {
			stop_listening(data);
			sListenLooper->Unlock();
		}
		return B_OK;
	}

	BLooper* looper = listen_looper();
	if (looper == NULL)
		return B_NO_MEMORY;

	BAutolock locker(looper);
	if (data->listener != NULL) {
		data->callback = callback;
		data->cookie = cookie;
		return B_OK;
	}

	// watch the set directory: saves replace the file by renaming over it
	BPath directoryPath;
	status_t status = set_directory(data, directoryPath);
	if (status == B_OK)
		status = create_directory(directoryPath.Path(), 0755);
	if (status != B_OK)
		return status;

	BDirectory directory(directoryPath.Path());
	node_ref ref;
	status = directory.InitCheck();
	if (status == B_OK)
		status = directory.GetNodeRef(&ref);
	if (status != B_OK)
		return status;

	_PREFListener* listener = new(std::nothrow) _PREFListener(data);
	if (listener == NULL)
		return B_NO_MEMORY;

	looper->AddHandler(listener);
	status = watch_node(&ref, B_WATCH_DIRECTORY, BMessenger(listener, looper));
	if (status != B_OK) {
		looper->RemoveHandler(listener);
		delete listener;
		return status;
	}

	data->listener = listener;
	data->callback = callback;
	data->cookie = cookie;
	return B_OK;
}
