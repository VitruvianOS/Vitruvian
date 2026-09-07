/*
 * Copyright 2026 Angelo Scarnà <angelo.scarna@primanotanet.it>. All rights reserved.
 *
 *
 * Copyright (c) 2008 Stephan Aßmus <superstippi@gmx.de>. All rights reserved.
 * Distributed under the terms of the MIT/X11 license.
 *
 * Copyright (c) 1999 Mike Steed. You are free to use and distribute this software
 * as long as it is accompanied by it's documentation and this copyright notice.
 * The software comes with no warranty, etc.
 */


#include "Scanner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <Catalog.h>
#include <Directory.h>
#include <Entry.h>
#include <Path.h>

#include <MountInfo.h>

#include "DiskUsage.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Scanner"

using std::vector;

Scanner::Scanner(BVolume *v, BHandler *handler)
	:
	BLooper(),
	fListener(handler),
	fDoneMessage(kScanDone),
	fProgressMessage(kScanProgress),
	fVolume(v),
	fSnapshot(NULL),
	fDesiredPath(),
	fTask(),
	fBusy(false),
	fQuitRequested(false)
{
	Run();
}


Scanner::~Scanner()
{
	delete fSnapshot;
}


void
Scanner::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kScanRefresh:
		{
			FileInfo* startInfo;
			if (message->FindPointer(kNameFilePtr, (void **)&startInfo) 
				== B_OK)
				_RunScan(startInfo);
			break;
		}

		default:
			BLooper::MessageReceived(message);
			break;
	}
}


void
Scanner::Refresh(FileInfo* startInfo)
{
	if (fBusy)
		return;

	fBusy = true;

	// Remember the current directory, if any, so we can return to it after
	// the scanning is done.
	if (fSnapshot != NULL && fSnapshot->currentDir != NULL)
		fSnapshot->currentDir->GetPath(fDesiredPath);

	fTask.assign(kEmptyStr);

	BMessage msg(kScanRefresh);
	msg.AddPointer(kNameFilePtr, startInfo);
	PostMessage(&msg);
}


void
Scanner::Cancel()
{
	if (!fBusy)
		return;

	fQuitRequested = true;
}


void
Scanner::SetDesiredPath(string &path)
{
	fDesiredPath = path;

	if (fSnapshot == NULL)
		Refresh();
	else
		_ChangeToDesired();
}


void
Scanner::RequestQuit()
{
	// If the looper thread is scanning, we won't succeed to grab the lock,
	// since the thread is scanning from within MessageReceived(). Then by
	// setting fQuitRequested, the thread will stop scanning and only after
	// that happened we succeed to grab the lock. So this line of action
	// should be safe.
	fQuitRequested = true;
	Lock();
	Quit();
}


// #pragma mark - private


bool
Scanner::_DirectoryContains(FileInfo* currentDir, entry_ref* ref)
{
	vector<FileInfo*>::iterator i = currentDir->children.begin();
	bool contains = currentDir->ref == *ref;
	while (!contains && i != currentDir->children.end()) {
		contains |= _DirectoryContains((*i), ref);
		i++;
	}
	return contains;
}


void
Scanner::_RunScan(FileInfo* startInfo)
{
	fQuitRequested = false;

	// Work out which device IDs belong to this volume and which are
	// foreign filesystems mounted inside it, so the walk can stop at
	// mount boundaries. Also reset the per-scan dedup/loop-guard sets.
	fVolumeDev = fVolume->Device();
	fSubmountDevs.clear();
	fSeenInodes.clear();
	fVisitedDirs.clear();

	BString rootPath;
	BPrivate::MountEntry rootMount;
	if (BPrivate::MountInfo::FindByDev(fVolumeDev, &rootMount))
		rootPath = rootMount.mount_point;

	BPrivate::MountSnapshot mounts = BPrivate::MountInfo::Snapshot();
	if (mounts != NULL) {
		BString prefix(rootPath);
		if (prefix.Length() > 0 && prefix.ByteAt(prefix.Length() - 1) != '/')
			prefix << "/";
		for (size_t i = 0; i < mounts->size(); i++) {
			const BPrivate::MountEntry& e = (*mounts)[i];
			if (e.dev == fVolumeDev)
				continue;
			// If we could not resolve our own mount point, be strict:
			// treat every other device as foreign (bounds the scan to a
			// single filesystem). Otherwise only mounts at or below our
			// mount point count as foreign.
			if (rootPath.Length() == 0 || rootPath == "/"
					|| e.mount_point == rootPath
					|| e.mount_point.StartsWith(prefix.String())) {
				fSubmountDevs.insert(e.dev);
			}
		}
	}


	BString stringScan(B_TRANSLATE("Scanning %refName%"));

	if (startInfo == NULL || startInfo == fSnapshot->rootDir) {
		VolumeSnapshot* previousSnapshot = fSnapshot;
		fSnapshot = new VolumeSnapshot(fVolume);
		stringScan.ReplaceFirst("%refName%", fSnapshot->name.c_str());
		fTask = stringScan.String();
		fVolumeBytesInUse = fSnapshot->capacity - fSnapshot->freeBytes;
		// Read-only media (squashfs) report free == capacity; keep the
		// progress fraction finite.
		if (fVolumeBytesInUse <= 0)
			fVolumeBytesInUse = fSnapshot->capacity > 0 ? fSnapshot->capacity : 1;
		fVolumeBytesScanned = 0;
		fProgress = 0.0;
		fLastReport = -1.0;

		BDirectory root;
		fVolume->GetRootDirectory(&root);
		fSnapshot->rootDir = _GetFileInfo(&root, NULL);
		if (fSnapshot->rootDir == NULL) {
			delete fSnapshot;
			fSnapshot = previousSnapshot;
			fBusy = false;
			fListener.SendMessage(&fDoneMessage);
			return;
		}
		FileInfo* freeSpace = new FileInfo;
		freeSpace->pseudo = true;
		BString string(B_TRANSLATE("Free on %refName%"));
		string.ReplaceFirst("%refName%", fSnapshot->name.c_str());
		freeSpace->ref.set_name(string.String());
		freeSpace->size = fSnapshot->freeBytes;
		fSnapshot->freeSpace = freeSpace;

		fSnapshot->currentDir = NULL;

		delete previousSnapshot;
	} else {
		off_t previousVolumeCapacity = fSnapshot->capacity;
		off_t previousVolumeFreeBytes = fSnapshot->freeBytes;
		fSnapshot->capacity = fVolume->Capacity();
		fSnapshot->freeBytes = fVolume->FreeBytes();
		stringScan.ReplaceFirst("%refName%", startInfo->ref.name);
		fTask = stringScan.String();
		fVolumeBytesInUse = fSnapshot->capacity - fSnapshot->freeBytes;
		if (fVolumeBytesInUse <= 0)
			fVolumeBytesInUse = fSnapshot->capacity > 0 ? fSnapshot->capacity : 1;
		fVolumeBytesScanned = fVolumeBytesInUse - startInfo->size; //best guess
		fProgress = fVolumeBytesScanned / fVolumeBytesInUse;
		fLastReport = -1.0;

		BDirectory startDir(&startInfo->ref);
		if (startDir.InitCheck() == B_OK) {
			FileInfo *parent = startInfo->parent;
			vector<FileInfo *>::iterator i = parent->children.begin();
			FileInfo* newInfo = _GetFileInfo(&startDir, parent);
			if (newInfo == NULL) {
				fSnapshot->capacity = previousVolumeCapacity;
				fSnapshot->freeBytes = previousVolumeFreeBytes;
				fBusy = false;
				fListener.SendMessage(&fDoneMessage);
				return;				
			}
			while (i != parent->children.end() && *i != startInfo)
				i++;

			int idx = i - parent->children.begin();
			parent->children[idx] = newInfo;

			// Fixup count and size fields in parent directory.
			off_t sizeDiff = newInfo->size - startInfo->size;
			off_t countDiff = newInfo->count - startInfo->count;
			while (parent != NULL) {
				parent->size += sizeDiff;
				parent->count += countDiff;
				parent = parent->parent;
			}

			delete startInfo;
		}
	}
	fBusy = false;
	_ChangeToDesired();
	fListener.SendMessage(&fDoneMessage);
}


FileInfo*
Scanner::_GetFileInfo(BDirectory* dir, FileInfo* parent)
{
	FileInfo* thisDir = new FileInfo;
	BEntry entry;
	dir->GetEntry(&entry);
	entry.GetRef(&thisDir->ref);
	thisDir->parent = parent;
	thisDir->count = 0;

	// An unreadable directory (permission denied, a vanished mount, a
	// pseudo-fs entry) still counts as one node but contributes nothing.
	// Without this guard GetNextEntry() below can spin without ever
	// returning B_ENTRY_NOT_FOUND.
	if (dir->InitCheck() != B_OK)
		return thisDir;

	// Remember this directory so a bind mount or fs quirk pointing back
	// at an ancestor cannot send the walk into an endless loop.
	struct stat dirStat;
	if (dir->GetStat(&dirStat) == B_OK)
		fVisitedDirs.insert(std::make_pair(dirStat.st_dev, dirStat.st_ino));

	while (true) {
		if (fQuitRequested) {
			delete thisDir;
			return NULL;
		}

		if (dir->GetNextEntry(&entry) != B_OK)
			break;
		if (entry.IsSymLink())
			continue;


		if (entry.IsFile()) {
			struct stat st;
			if (entry.GetStat(&st) != B_OK)
				continue;

			// BFS had no file hard links; on ext4/xfs (.git trees,
			// package caches, backups) they are everywhere. Count the
			// inode once per scan.
			if (st.st_nlink > 1 && !fSeenInodes.insert(
					std::make_pair(st.st_dev, st.st_ino)).second) {
				thisDir->count++;
				continue;
			}

			FileInfo *child = new FileInfo;
			entry.GetRef(&child->ref);
			// Allocated blocks, not the apparent size: keeps sparse
			// files and transparent btrfs/xfs compression honest.
			child->size = (off_t)st.st_blocks * 512;
			child->parent = thisDir;
			child->color = -1;
			thisDir->children.push_back(child);

			// Send a progress report periodically.
			fVolumeBytesScanned += child->size;
			fProgress = (float)fVolumeBytesScanned / fVolumeBytesInUse;
			if (fProgress - fLastReport > kReportInterval) {
				fLastReport = fProgress;
				fListener.SendMessage(&fProgressMessage);
			}
		}
		else if (entry.IsDirectory()) {
			struct stat st;
			if (entry.GetStat(&st) != B_OK) {
				thisDir->count++;
				continue;
			}
			// Don't cross into another filesystem mounted inside this
			// volume (another disk, /proc, /sys, a separately mounted
			// btrfs subvolume...).
			if (_IsForeignMount(st.st_dev)) {
				thisDir->count++;
				continue;
			}
			// Already walked this exact directory (bind mount / loop).
			if (!fVisitedDirs.insert(
					std::make_pair(st.st_dev, st.st_ino)).second) {
				thisDir->count++;
				continue;
			}
			BDirectory childDir(&entry);
			if (childDir.InitCheck() != B_OK) {
				thisDir->count++;
				continue;
			}
			FileInfo* childInfo = _GetFileInfo(&childDir, thisDir);
			if (childInfo == NULL) {
				// Quit requested somewhere below: unwind cleanly instead
				// of storing a NULL child the aggregation would deref.
				delete thisDir;
				return NULL;
			}
			thisDir->children.push_back(childInfo);
		}
		thisDir->count++;
	}

	vector<FileInfo *>::iterator i = thisDir->children.begin();
	while (i != thisDir->children.end()) {
		thisDir->size += (*i)->size;
		thisDir->count += (*i)->count;
		i++;
	}

	return thisDir;
}


bool
Scanner::_IsForeignMount(dev_t childDev) const
{
	// The volume's own device is never foreign. btrfs subvolumes of the
	// same filesystem carry their own anonymous device but are not listed
	// in /proc/self/mountinfo, so they are not in fSubmountDevs and get
	// walked normally. Everything actually mounted inside the volume is.
	return childDev != fVolumeDev
		&& fSubmountDevs.find(childDev) != fSubmountDevs.end();
}


void
Scanner::_ChangeToDesired()
{
	if (fBusy || fDesiredPath.size() <= 0)
		return;

	char* workPath = strdup(fDesiredPath.c_str());
	char* pathPtr = &workPath[1]; // skip leading '/'
	char* endOfPath = pathPtr + strlen(pathPtr);

	// Check the name of the root directory.  It is a special case because
	// it has no parent data structure.
	FileInfo* checkDir = fSnapshot->rootDir;
	FileInfo* prefDir = NULL;
	char* sep = strchr(pathPtr, '/');
	if (sep != NULL)
		*sep = '\0';

	if (strcmp(pathPtr, checkDir->ref.name) == 0) {
		// Root directory matches, so follow the remainder of the path as
		// far as possible.
		prefDir = checkDir;
		pathPtr += 1 + strlen(pathPtr);
		while (pathPtr < endOfPath) {
			sep = strchr(pathPtr, '/');
			if (sep != NULL)
				*sep = '\0';

			checkDir = prefDir->FindChild(pathPtr);
			if (checkDir == NULL || checkDir->children.size() == 0)
				break;

			pathPtr += 1 + strlen(pathPtr);
			prefDir = checkDir;
		}
	}

	// If the desired path is the volume's root directory, default to the
	// volume display.
	if (prefDir == fSnapshot->rootDir)
		prefDir = NULL;

	ChangeDir(prefDir);

	free(workPath);
	fDesiredPath.erase();
}
