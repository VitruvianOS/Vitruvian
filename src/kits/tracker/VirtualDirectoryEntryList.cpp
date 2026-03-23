/*
 * Copyright 2013, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Ingo Weinhold, ingo_weinhold@gmx.de
 */


#include "VirtualDirectoryEntryList.h"

#include <AutoLocker.h>
#include <storage_support.h>

#include "Model.h"
#include "VirtualDirectoryManager.h"


namespace BPrivate {

//	#pragma mark - VirtualDirectoryEntryList


VirtualDirectoryEntryList::VirtualDirectoryEntryList(Model* model)
	:
	EntryListBase(),
	fDefinitionFileRef(),
	fMergedDirectory(BMergedDirectory::B_ALWAYS_FIRST)
{

	VirtualDirectoryManager* manager = VirtualDirectoryManager::Instance();
	if (manager == NULL) {
		fStatus = B_NO_MEMORY;
		return;
	}

	AutoLocker<VirtualDirectoryManager> managerLocker(manager);
	BStringList directoryPaths;
	fStatus = manager->ResolveDirectoryPaths(*model->NodeRef(),
		*model->EntryRef(), directoryPaths, &fDefinitionFileRef);
	if (fStatus != B_OK)
		return;

	fStatus = _InitMergedDirectory(directoryPaths);
}


VirtualDirectoryEntryList::VirtualDirectoryEntryList(
	const node_ref& definitionFileRef, const BStringList& directoryPaths)
	:
	EntryListBase(),
	fDefinitionFileRef(definitionFileRef),
	fMergedDirectory(BMergedDirectory::B_ALWAYS_FIRST)
{
	fStatus = _InitMergedDirectory(directoryPaths);
}


VirtualDirectoryEntryList::~VirtualDirectoryEntryList()
{
}


status_t
VirtualDirectoryEntryList::InitCheck() const
{
	return EntryListBase::InitCheck();
}


status_t
VirtualDirectoryEntryList::GetNextEntry(BEntry* entry, bool traverse)
{
	entry_ref ref;
	status_t error = GetNextRef(&ref);
	if (error != B_OK)
		return error;

	return entry->SetTo(&ref, traverse);
}


status_t
VirtualDirectoryEntryList::GetNextRef(entry_ref* ref)
{
	// Delegate to BMergedDirectory::GetNextRef which correctly resolves the
	// parent directory device/inode via BDirectory on Linux (where struct
	// dirent does not carry d_pdev/d_pino).
	status_t error = fMergedDirectory.GetNextRef(ref);
	if (error != B_OK)
		return error;

	// Translate subdirectory entries into virtual directory references.
	if (BEntry(ref).IsDirectory()) {
		if (VirtualDirectoryManager* manager
				= VirtualDirectoryManager::Instance()) {
			AutoLocker<VirtualDirectoryManager> managerLocker(manager);
			node_ref node(ref->dev(), ref->dir());
			manager->TranslateDirectoryEntry(fDefinitionFileRef, *ref, node);
		}
	}

	return B_OK;
}


int32
VirtualDirectoryEntryList::GetNextDirents(struct dirent* buffer, size_t length,
	int32 count)
{
	if (count > 1)
		count = 1;

	int32 countRead = fMergedDirectory.GetNextDirents(buffer, length, count);
	if (countRead != 1)
		return countRead;

	// Directory translation for GetNextDirents callers is handled in
	// GetNextRef via fMergedDirectory.GetNextRef which has the correct
	// parent directory context.
	return countRead;
}


status_t
VirtualDirectoryEntryList::Rewind()
{
	return fMergedDirectory.Rewind();
}


int32
VirtualDirectoryEntryList::CountEntries()
{
	return 0;
}


status_t
VirtualDirectoryEntryList::_InitMergedDirectory(
	const BStringList& directoryPaths)
{
	status_t error = fMergedDirectory.Init();
	if (error != B_OK)
		return error;

	int32 count = directoryPaths.CountStrings();
	int32 added = 0;
	for (int32 i = 0; i < count; i++) {
		status_t addErr = fMergedDirectory.AddDirectory(directoryPaths.StringAt(i));
		if (addErr == B_OK)
			added++;
	}

	return B_OK;
}

} // namespace BPrivate
