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


#include "TrashWatcher.h"

#include <string.h>

#include <Debug.h>
#include <Directory.h>
#include <Entry.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include "Attributes.h"
#include "Bitmaps.h"
#include "FSUtils.h"
#include "Tracker.h"


// Resolve the sibling info/ dir for a given Trash files/ dir.
// Returns the dir path in *outPath; does not require it to exist.
static status_t
_trash_info_dir_for_files(const BDirectory& filesDir, BPath* outPath)
{
	BEntry entry;
	status_t r = filesDir.GetEntry(&entry);
	if (r != B_OK)
		return r;
	BPath filesPath;
	r = entry.GetPath(&filesPath);
	if (r != B_OK)
		return r;
	BPath trashRoot;
	r = filesPath.GetParent(&trashRoot);
	if (r != B_OK)
		return r;
	return outPath->SetTo(trashRoot.Path(), "info");
}


//	 #pragma mark - BTrashWatcher


BTrashWatcher::BTrashWatcher()
	:
	BLooper("TrashWatcher", B_LOW_PRIORITY),
	fTrashNodeList(20)
{
	FSCreateTrashDirs();
	WatchTrashDirs();
	fTrashFull = CheckTrashDirs();
	UpdateTrashIcons();

	// watch volumes
	TTracker::WatchNode(0, B_WATCH_MOUNT, this);
}


BTrashWatcher::~BTrashWatcher()
{
	stop_watching(this);
}


bool
BTrashWatcher::IsTrashNode(const node_ref* testNode) const
{
	int32 count = fTrashNodeList.CountItems();
	for (int32 index = 0; index < count; index++) {
		node_ref* nref = fTrashNodeList.ItemAt(index);
		if (nref->dereference().ino() == testNode->dereference().ino() && nref->dereference().dev() == testNode->dereference().dev())
			return true;
	}

	return false;
}


void
BTrashWatcher::MessageReceived(BMessage* message)
{
	if (message->what != B_NODE_MONITOR) {
		_inherited::MessageReceived(message);
		return;
	}

	switch (message->GetInt32("opcode", 0)) {
		case B_ENTRY_CREATED:
			if (!fTrashFull) {
				fTrashFull = true;
				UpdateTrashIcons();
			}
			break;

		case B_ENTRY_MOVED:
		{
			// allow code to fall through if move is from/to trash
			// but do nothing for moves in the same directory
			entry_ref toDir;
			entry_ref fromDir;
			message->FindRef("virtual:from directory", &fromDir);
			message->FindRef("virtual:to directory", &toDir);
			if (fromDir.dev() == toDir.dev() && fromDir.dir() == toDir.dir())
				break;
		}
		// fall-through
		case B_DEVICE_UNMOUNTED:
		case B_ENTRY_REMOVED:
		{
			ReconcileTrashDirs();
			bool full = CheckTrashDirs();
			if (fTrashFull != full) {
				fTrashFull = full;
				UpdateTrashIcons();
			}
			break;
		}
		// We should handle DEVICE_UNMOUNTED here too to remove trash

		case B_DEVICE_MOUNTED:
		{
			dev_t device;
			BDirectory trashDir;
			if (message->FindUInt64("new device", (uint64*)&device) == B_OK
				&& FSGetTrashDir(&trashDir, device) == B_OK) {
				node_ref trashNode;
				trashDir.GetNodeRef(&trashNode);
				TTracker::WatchNode(&trashNode, B_WATCH_DIRECTORY, this);
				fTrashNodeList.AddItem(new node_ref(trashNode));

				// Check if the new volume has anything trashed.
				if (CheckTrashDirs() && !fTrashFull) {
					fTrashFull = true;
					UpdateTrashIcons();
				}
			}
			break;
		}
	}
}


void
BTrashWatcher::UpdateTrashIcons()
{
	// only update Trash icon attributes on boot volume
	BVolume boot;
	BDirectory trashDir;
	if (BVolumeRoster().GetBootVolume(&boot) == B_OK
		&& FSGetTrashDir(&trashDir, boot.Device()) == B_OK) {
		// pull out the icons for the current trash state from resources
		// and apply them onto the trash directory node
		int32 id = fTrashFull ? R_TrashFullIcon : R_TrashIcon;
		size_t size = 0;
		const void* data = GetTrackerResources()->LoadResource(B_VECTOR_ICON_TYPE, id, &size);
		if (data != NULL && size > 0) {
			// write vector icon attribute
			trashDir.WriteAttr(kAttrIcon, B_VECTOR_ICON_TYPE, 0, data, size);
		} else {
			// write large and mini icon attributes
			data = GetTrackerResources()->LoadResource('ICON', id, &size);
			if (data != NULL && size > 0)
				trashDir.WriteAttr(kAttrLargeIcon, 'ICON', 0, data, size);

			data = GetTrackerResources()->LoadResource('MICN', id, &size);
			if (data != NULL && size > 0)
				trashDir.WriteAttr(kAttrMiniIcon, 'MICN', 0, data, size);
		}
	}
}


void
BTrashWatcher::WatchTrashDirs()
{
	BVolumeRoster volRoster;
	volRoster.Rewind();
	BVolume	volume;
	while (volRoster.GetNextVolume(&volume) == B_OK) {
		// IsPersistent dropped: overlayfs reports non-persistent.
		if (volume.IsReadOnly())
			continue;

		BDirectory trashDir;
		if (FSGetTrashDir(&trashDir, volume.Device()) == B_OK) {
			node_ref trash_node;
			trashDir.GetNodeRef(&trash_node);
			watch_node(&trash_node, B_WATCH_DIRECTORY, this);
			fTrashNodeList.AddItem(new node_ref(trash_node));

			// Also watch the sibling info/ dir so out-of-band tools
			// (e.g. `gio trash`, manual rm) trigger reconciliation.
			BPath infoPath;
			if (_trash_info_dir_for_files(trashDir, &infoPath) == B_OK) {
				BDirectory infoDir(infoPath.Path());
				if (infoDir.InitCheck() == B_OK) {
					node_ref info_node;
					infoDir.GetNodeRef(&info_node);
					watch_node(&info_node, B_WATCH_DIRECTORY, this);
				}
			}
		}
	}
}


void
BTrashWatcher::ReconcileTrashDirs()
{
	// Prune orphan .trashinfo entries (a .trashinfo whose paired entry
	// under files/ has gone away — typically because someone deleted
	// from files/ out of band, or because a previous move-to-trash
	// failed between the rename and the trashinfo write).
	BVolumeRoster volRoster;
	volRoster.Rewind();
	BVolume volume;
	while (volRoster.GetNextVolume(&volume) == B_OK) {
		if (volume.IsReadOnly())
			continue;

		BDirectory filesDir;
		if (FSGetTrashDir(&filesDir, volume.Device()) != B_OK)
			continue;
		BPath infoPath;
		if (_trash_info_dir_for_files(filesDir, &infoPath) != B_OK)
			continue;

		BDirectory infoDir(infoPath.Path());
		if (infoDir.InitCheck() != B_OK)
			continue;

		infoDir.Rewind();
		BEntry infoEntry;
		while (infoDir.GetNextEntry(&infoEntry) == B_OK) {
			char name[B_FILE_NAME_LENGTH];
			if (infoEntry.GetName(name) != B_OK)
				continue;
			size_t len = strlen(name);
			static const char kSuffix[] = ".trashinfo";
			const size_t kSuffixLen = sizeof(kSuffix) - 1;
			if (len <= kSuffixLen
				|| strcmp(name + len - kSuffixLen, kSuffix) != 0) {
				continue;
			}
			BString base(name, len - kSuffixLen);
			if (!filesDir.Contains(base.String()))
				infoEntry.Remove();
		}
	}
}


bool
BTrashWatcher::CheckTrashDirs()
{
	BVolumeRoster volRoster;
	volRoster.Rewind();
	BVolume	volume;
	while (volRoster.GetNextVolume(&volume) == B_OK) {
		if (volume.IsReadOnly() || volume.Capacity() == 0)
			continue;

		BDirectory trashDir;
		FSGetTrashDir(&trashDir, volume.Device());
		trashDir.Rewind();
		BEntry entry;
		if (trashDir.GetNextEntry(&entry) == B_OK)
			return true;
	}

	return false;
}
