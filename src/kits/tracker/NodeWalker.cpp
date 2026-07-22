/*
Open Tracker License

Terms and Conditions

Copyright (c) 1991-2000, Be Incorporated. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice applies to all licensees
and shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of Be Incorporated shall not be
used in advertising or otherwise to promote the sale, use or other dealings in
this Software without prior written authorization from Be Incorporated.

Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered trademarks
of Be Incorporated in the United States and other countries. Other brand product
names are registered trademarks or trademarks of their respective holders.
All rights reserved.
*/


#include <Debug.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <SupportDefs.h>

#include <AppFileInfo.h>
#include <File.h>

#include "NodeWalker.h"


namespace BTrackerPrivate {

TWalker::~TWalker()
{
}


// all the following calls are pure virtuals, should not get called
status_t
TWalker::GetNextEntry(BEntry*, bool )
{
	TRESPASS();
	return B_ERROR;
}


status_t
TWalker::GetNextRef(entry_ref*)
{
	TRESPASS();
	return B_ERROR;
}


int32
TWalker::GetNextDirents(struct dirent*, size_t, int32)
{
	TRESPASS();
	return 0;
}


status_t
TWalker::Rewind()
{
	TRESPASS();
	return B_ERROR;
}


int32
TWalker::CountEntries()
{
	TRESPASS();
	return -1;
}


TNodeWalker::TNodeWalker(bool includeTopDirectory)
	:
	fDirs(20),
	fTopIndex(-1),
	fTopDir(NULL),
	fIncludeTopDir(includeTopDirectory),
	fOriginalIncludeTopDir(includeTopDirectory),
	fJustFile(NULL),
	fOriginalJustFile(NULL)
{
}


TNodeWalker::TNodeWalker(const char* path, bool includeTopDirectory)
	:
	fDirs(20),
	fTopIndex(-1),
	fTopDir(NULL),
	fIncludeTopDir(includeTopDirectory),
	fOriginalIncludeTopDir(includeTopDirectory),
	fJustFile(NULL),
	fOriginalDirCopy(path),
	fOriginalJustFile(NULL)
{
	if (fOriginalDirCopy.InitCheck() != B_OK) {
		// not a directory, set up walking a single file
		fJustFile = new BEntry(path);
		if (fJustFile->InitCheck() != B_OK) {
			delete fJustFile;
			fJustFile = NULL;
		}
		fOriginalJustFile = fJustFile;
	} else {
		fTopDir = new BDirectory(fOriginalDirCopy);
		fTopIndex++;
		fDirs.AddItem(fTopDir);
	}
}


TNodeWalker::TNodeWalker(const entry_ref* ref, bool includeTopDirectory)
	:
	fDirs(20),
	fTopIndex(-1),
	fTopDir(NULL),
	fIncludeTopDir(includeTopDirectory),
	fOriginalIncludeTopDir(includeTopDirectory),
	fJustFile(NULL),
	fOriginalDirCopy(ref),
	fOriginalJustFile(NULL)
{
	if (fOriginalDirCopy.InitCheck() != B_OK) {
		// not a directory, set up walking a single file
		fJustFile = new BEntry(ref);
		if (fJustFile->InitCheck() != B_OK) {
			delete fJustFile;
			fJustFile = NULL;
		}
		fOriginalJustFile = fJustFile;
	} else {
		fTopDir = new BDirectory(fOriginalDirCopy);
		fTopIndex++;
		fDirs.AddItem(fTopDir);
	}
}


TNodeWalker::TNodeWalker(const BDirectory* dir, bool includeTopDirectory)
	:
	fDirs(20),
	fTopIndex(-1),
	fTopDir(NULL),
	fIncludeTopDir(includeTopDirectory),
	fOriginalIncludeTopDir(includeTopDirectory),
	fJustFile(NULL),
	fOriginalDirCopy(*dir),
	fOriginalJustFile(NULL)
{
	fTopDir = new BDirectory(*dir);
	fTopIndex++;
	fDirs.AddItem(fTopDir);
}


TNodeWalker::TNodeWalker()
	:
	fDirs(20),
	fTopIndex(-1),
	fTopDir(NULL),
	fIncludeTopDir(false),
	fOriginalIncludeTopDir(false),
	fJustFile(NULL),
	fOriginalJustFile(NULL)
{
}


TNodeWalker::TNodeWalker(const char* path)
	:
	fDirs(20),
	fTopIndex(-1),
	fTopDir(NULL),
	fIncludeTopDir(false),
	fOriginalIncludeTopDir(false),
	fJustFile(NULL),
	fOriginalDirCopy(path),
	fOriginalJustFile(NULL)
{
	if (fOriginalDirCopy.InitCheck() != B_OK) {
		// not a directory, set up walking a single file
		fJustFile = new BEntry(path);
		if (fJustFile->InitCheck() != B_OK) {
			delete fJustFile;
			fJustFile = NULL;
		}
		fOriginalJustFile = fJustFile;
	} else {
		fTopDir = new BDirectory(fOriginalDirCopy);
		fTopIndex++;
		fDirs.AddItem(fTopDir);
	}
}


TNodeWalker::TNodeWalker(const entry_ref* ref)
	:
	fDirs(20),
	fTopIndex(-1),
	fTopDir(NULL),
	fIncludeTopDir(false),
	fOriginalIncludeTopDir(false),
	fJustFile(NULL),
	fOriginalDirCopy(ref),
	fOriginalJustFile(NULL)
{
	if (fOriginalDirCopy.InitCheck() != B_OK) {
		// not a directory, set up walking a single file
		fJustFile = new BEntry(ref);
		if (fJustFile->InitCheck() != B_OK) {
			delete fJustFile;
			fJustFile = NULL;
		}
		fOriginalJustFile = fJustFile;
	} else {
		fTopDir = new BDirectory(fOriginalDirCopy);
		fTopIndex++;
		fDirs.AddItem(fTopDir);
	}
}

TNodeWalker::TNodeWalker(const BDirectory* dir)
	:
	fDirs(20),
	fTopIndex(-1),
	fTopDir(NULL),
	fIncludeTopDir(false),
	fOriginalIncludeTopDir(false),
	fJustFile(NULL),
	fOriginalDirCopy(*dir),
	fOriginalJustFile(NULL)
{
	fTopDir = new BDirectory(*dir);
	fTopIndex++;
	fDirs.AddItem(fTopDir);
}


TNodeWalker::~TNodeWalker()
{
	delete fOriginalJustFile;

	for (;;) {
		BDirectory* directory = fDirs.RemoveItemAt(fTopIndex--);
		if (directory == NULL)
			break;

		delete directory;
	}
}


status_t
TNodeWalker::PopDirCommon()
{
	ASSERT(fTopIndex >= 0);

	// done with the old dir, pop it
	fDirs.RemoveItemAt(fTopIndex);
	fTopIndex--;
	delete fTopDir;
	fTopDir = NULL;

	if (fTopIndex == -1) {
		// done
		return B_ENTRY_NOT_FOUND;
	}

	// point to the new top dir
	fTopDir = fDirs.ItemAt(fTopIndex);

	return B_OK;
}


void
TNodeWalker::PushDirCommon(const entry_ref* ref)
{
	fTopDir = new BDirectory(ref);
		// OK to ignore error here. Will
		// catch at next call to GetNextEntry
	fTopIndex++;
	fDirs.AddItem(fTopDir);
}


status_t
TNodeWalker::GetNextEntry(BEntry* entry, bool traverse)
{
	if (fJustFile != NULL) {
		*entry = *fJustFile;
		fJustFile = 0;
		return B_OK;
	}

	if (fTopDir == NULL) {
		// done
		return B_ENTRY_NOT_FOUND;
	}

	// If requested to include the top directory, return that first.
	if (fIncludeTopDir) {
		fIncludeTopDir = false;
		return fTopDir->GetEntry(entry);
	}

	// Get the next entry.
	status_t result = fTopDir->GetNextEntry(entry, traverse);
	if (result != B_OK) {
		result = PopDirCommon();
		if (result != B_OK)
			return result;

		return GetNextEntry(entry, traverse);
	}
	// See if this entry is a directory. If it is then push it onto the
	// stack
	entry_ref ref;
	result = entry->GetRef(&ref);

	if (result == B_OK && fTopDir->Contains(ref.name, B_DIRECTORY_NODE))
		PushDirCommon(&ref);

	return result;
}


status_t
TNodeWalker::GetNextRef(entry_ref* ref)
{
	if (fJustFile != NULL) {
		fJustFile->GetRef(ref);
		fJustFile = 0;
		return B_OK;
	}

	if (fTopDir == NULL) {
		// done
		return B_ENTRY_NOT_FOUND;
	}

	// If requested to include the top directory, return that first.
	if (fIncludeTopDir) {
		fIncludeTopDir = false;
		BEntry entry;
		status_t err = fTopDir->GetEntry(&entry);
		if (err == B_OK)
			err = entry.GetRef(ref);
		return err;
	}

	// get the next entry
	status_t err = fTopDir->GetNextRef(ref);
	if (err != B_OK) {
		err = PopDirCommon();
		if (err != B_OK)
			return err;
		return GetNextRef(ref);
	}

	// See if this entry is a directory, if it is then push it onto the stack.
	if (fTopDir->Contains(ref->name, B_DIRECTORY_NODE))
		PushDirCommon(ref);

	return B_OK;
}


static int32
build_dirent(const BEntry* source, struct dirent* ent,
	size_t size, int32 count)
{
	if (source == NULL)
		return 0;

	entry_ref ref;
	source->GetRef(&ref);

	size_t recordLength = strlen(ref.name) + sizeof(dirent);
	if (recordLength > size || count <= 0) {
		// can't fit in buffer, bail
		return 0;
	}

	// info about this node
	ent->d_reclen = static_cast<ushort>(recordLength);
	strcpy(ent->d_name, ref.name);
	ent->d_ino = ref.vdirectory();

	return 1;
}


int32
TNodeWalker::GetNextDirents(struct dirent* ent, size_t size, int32 count)
{
	if (fJustFile != NULL) {
		if (count == 0)
			return 0;

		// simulate GetNextDirents by building a single dirent structure
		int32 result = build_dirent(fJustFile, ent, size, count);
		fJustFile = 0;
		return result;
	}

	if (fTopDir == NULL) {
		// done
		return 0;
	}

	// If requested to include the top directory, return that first.
	if (fIncludeTopDir) {
		fIncludeTopDir = false;
		BEntry entry;
		if (fTopDir->GetEntry(&entry) < B_OK)
			return 0;

		return build_dirent(fJustFile, ent, size, count);
	}

	// get the next entry
	int32 nextDirent = fTopDir->GetNextDirents(ent, size, count);
	if (nextDirent == 0) {
		status_t result = PopDirCommon();
		if (result != B_OK)
			return 0;

		return GetNextDirents(ent, size, count);
	}

	node_ref dirNodeRef;
	fTopDir->GetNodeRef(&dirNodeRef);
	for (int32 i = 0; i < nextDirent; i++) {
		bool isDir = ent->d_type == DT_DIR
			|| ((ent->d_type == DT_UNKNOWN || ent->d_type == DT_LNK)
				&& fTopDir->Contains(ent->d_name, B_DIRECTORY_NODE));
		if (isDir) {
			entry_ref ref(dirNodeRef.vdevice(), dirNodeRef.vnode(), ent->d_name);
			PushDirCommon(&ref);
		}
		ent = (dirent*)((char*)ent + ent->d_reclen);
	}

	return nextDirent;
}


status_t
TNodeWalker::Rewind()
{
	if (fOriginalJustFile != NULL) {
		// single file mode, rewind by pointing to the original file
		fJustFile = fOriginalJustFile;
		return B_OK;
	}

	// pop all the directories and point to the initial one
	for (;;) {
		BDirectory* directory = fDirs.RemoveItemAt(fTopIndex--);
		if (directory == NULL)
			break;

		delete directory;
	}

	fTopDir = new BDirectory(fOriginalDirCopy);
	fTopIndex = 0;
	fIncludeTopDir = fOriginalIncludeTopDir;
	fDirs.AddItem(fTopDir);

	return fTopDir->Rewind();
		// rewind the directory
}

int32
TNodeWalker::CountEntries()
{
	// should not be calling this
	TRESPASS();
	return -1;
}


TVolWalker::TVolWalker(bool knowsAttributes, bool writable,
	bool includeTopDirectory)
	:
	TNodeWalker(includeTopDirectory),
	fKnowsAttr(knowsAttributes),
	fWritable(writable)
{
	// Get things initialized. Find first volume, or find the first volume
	// that supports attributes.
 	NextVolume();
}


TVolWalker::~TVolWalker()
{
}


status_t
TVolWalker::NextVolume()
{
	// The stack of directoies should be empty.
	ASSERT(fTopIndex == -1);
	ASSERT(fTopDir == NULL);

	status_t result;
	do {
		result = fVolRoster.GetNextVolume(&fVol);
		if (result != B_OK)
			break;
	} while ((fKnowsAttr && !fVol.KnowsAttr())
		|| (fWritable && fVol.IsReadOnly()));

	if (result == B_OK) {
		// Get the root directory to get things started. There's always
		// a root directory for a volume. So if there is an error then it
		// means that something is really bad, like the system is out of
		// memory.  In that case don't worry about truying to skip to the
		// next volume.
		fTopDir = new BDirectory();
		result = fVol.GetRootDirectory(fTopDir);
		fIncludeTopDir = fOriginalIncludeTopDir;
		fTopIndex = 0;
		fDirs.AddItem(fTopDir);
	}

	return result;
}

status_t
TVolWalker::GetNextEntry(BEntry* entry, bool traverse)
{
	if (fTopDir == NULL)
		return B_ENTRY_NOT_FOUND;

	// get the next entry
	status_t result = _inherited::GetNextEntry(entry, traverse);
	while (result != B_OK) {
		// we're done with the current volume, go to the next one
		result = NextVolume();
		if (result != B_OK)
			break;

		result = GetNextEntry(entry, traverse);
	}

	return result;
}


status_t
TVolWalker::GetNextRef(entry_ref* ref)
{
	if (fTopDir == NULL)
		return B_ENTRY_NOT_FOUND;

	// Get the next ref.
	status_t result = _inherited::GetNextRef(ref);

	while (result != B_OK) {
		// we're done with the current volume, go to the next one
		result = NextVolume();
		if (result != B_OK)
			break;
		result = GetNextRef(ref);
	}

	return result;
}


int32
TVolWalker::GetNextDirents(struct dirent* ent, size_t size, int32 count)
{
	if (fTopDir == NULL)
		return B_ENTRY_NOT_FOUND;

	// get the next dirent
	status_t result = _inherited::GetNextDirents(ent, size, count);
	while (result != B_OK) {
		// we're done with the current volume, go to the next one
		result = NextVolume();
		if (result != B_OK)
			break;

		result = GetNextDirents(ent, size, count);
	}

	return result;
}


status_t
TVolWalker::Rewind()
{
	fVolRoster.Rewind();
	return NextVolume();
}


#ifdef __VOS__
static const char* const kVosAppScanDirs[] = {
	"/system/apps",
	"/system/preferences",
	"/system/servers",
	"/system",
	"/system/bin",
	NULL
};


static void
parse_app_sig_predicate(const char* predicate,
	BObjectList<BString, true>* allowedSigs, bool* acceptAny)
{
	// Extract every BEOS:APP_SIG = "..." or BEOS:APP_SIG = bareword clause.
	*acceptAny = false;
	const char* kAttr = "BEOS:APP_SIG";
	const char* p = predicate;
	while ((p = strstr(p, kAttr)) != NULL) {
		p += strlen(kAttr);
		while (*p == ' ' || *p == '=') p++;
		const char* end;
		BString sig;
		if (*p == '"') {
			p++;
			end = strchr(p, '"');
			if (end == NULL) break;
			sig.SetTo(p, end - p);
			p = end + 1;
		} else {
			end = p;
			while (*end && *end != ' ' && *end != ')' && *end != '|'
				&& *end != '&')
				end++;
			sig.SetTo(p, end - p);
			p = end;
		}
		if (sig == "*") {
			*acceptAny = true;
		} else if (sig.Length() > 0) {
			allowedSigs->AddItem(new BString(sig));
		}
	}
}
#endif


TQueryWalker::TQueryWalker(const char* predicate)
	:
	TWalker(),
	fTime(0)
{
	fPredicate = strdup(predicate);
#ifdef __VOS__
	fVosAppScan = false;
	fVosAcceptAny = false;
	fVosAllowedSigs = new BObjectList<BString, true>(20);
	fVosDirIndex = 0;
	if (predicate != NULL && strstr(predicate, "BEOS:APP_SIG") != NULL) {
		fVosAppScan = true;
		parse_app_sig_predicate(predicate, fVosAllowedSigs, &fVosAcceptAny);
		fVosCurrentDir.SetTo(kVosAppScanDirs[0]);
		return;
	}
#endif
	NextVolume();
}


TQueryWalker::~TQueryWalker()
{
	free((char*)fPredicate);
	fPredicate = NULL;
#ifdef __VOS__
	delete fVosAllowedSigs;
#endif
}


#ifdef __VOS__
static bool
vos_entry_matches(const entry_ref& ref, bool acceptAny,
	BObjectList<BString, true>* allowedSigs)
{
	BFile file(&ref, B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return false;
	BAppFileInfo info(&file);
	if (info.InitCheck() != B_OK)
		return false;
	char sig[B_MIME_TYPE_LENGTH];
	if (info.GetSignature(sig) != B_OK)
		return false;
	if (acceptAny)
		return true;
	for (int32 i = 0; i < allowedSigs->CountItems(); i++) {
		if (allowedSigs->ItemAt(i)->ICompare(sig) == 0)
			return true;
	}
	return false;
}


status_t
TQueryWalker::_VosGetNextRef(entry_ref* ref)
{
	for (;;) {
		entry_ref candidate;
		status_t status = fVosCurrentDir.GetNextRef(&candidate);
		if (status == B_OK) {
			if (vos_entry_matches(candidate, fVosAcceptAny, fVosAllowedSigs)) {
				*ref = candidate;
				return B_OK;
			}
			continue;
		}
		fVosDirIndex++;
		if (kVosAppScanDirs[fVosDirIndex] == NULL)
			return B_ENTRY_NOT_FOUND;
		fVosCurrentDir.SetTo(kVosAppScanDirs[fVosDirIndex]);
	}
}
#endif


status_t
TQueryWalker::GetNextEntry(BEntry* entry, bool traverse)
{
#ifdef __VOS__
	if (fVosAppScan) {
		entry_ref ref;
		status_t status = _VosGetNextRef(&ref);
		if (status != B_OK)
			return status;
		return entry->SetTo(&ref, traverse);
	}
#endif
	status_t result;
	do {
		result = fQuery.GetNextEntry(entry, traverse);
		if (result == B_ENTRY_NOT_FOUND) {
			if (NextVolume() != B_OK)
				break;
		}
	} while (result == B_ENTRY_NOT_FOUND);

	return result;
}


status_t
TQueryWalker::GetNextRef(entry_ref* ref)
{
#ifdef __VOS__
	if (fVosAppScan)
		return _VosGetNextRef(ref);
#endif
	status_t result;

	for (;;) {
		result = fQuery.GetNextRef(ref);
		if (result != B_ENTRY_NOT_FOUND)
			break;

		result = NextVolume();
		if (result != B_OK)
			break;
	}

	return result;
}


int32
TQueryWalker::GetNextDirents(struct dirent* ent, size_t size, int32 count)
{
	int32 result;

	for (;;) {
		result = fQuery.GetNextDirents(ent, size, count);
		if (result != 0)
			return result;

		if (NextVolume() != B_OK)
			return 0;
	}

	return result;
}


status_t
TQueryWalker::NextVolume()
{
	status_t result;
	do {
		result = fVolRoster.GetNextVolume(&fVol);
		if (result != B_OK)
			break;
	} while (!fVol.KnowsQuery());

	if (result == B_OK) {
		result = fQuery.Clear();
		result = fQuery.SetVolume(&fVol);
		result = fQuery.SetPredicate(fPredicate);
		result = fQuery.Fetch();
	}

	return result;
}


int32
TQueryWalker::CountEntries()
{
	// should not be calling this
	TRESPASS();
	return -1;
}


status_t
TQueryWalker::Rewind()
{
#ifdef __VOS__
	if (fVosAppScan) {
		fVosDirIndex = 0;
		fVosCurrentDir.SetTo(kVosAppScanDirs[0]);
		return B_OK;
	}
#endif
	fVolRoster.Rewind();
	return NextVolume();
}

}	// namespace BTrackerPrivate
