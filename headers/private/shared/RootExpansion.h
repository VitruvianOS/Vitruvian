/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _ROOT_EXPANSION_H
#define _ROOT_EXPANSION_H


#include <DiskDevice.h>
#include <DiskDeviceRoster.h>
#include <DiskSystem.h>
#include <Partition.h>
#include <PartitioningInfo.h>

#include <RootMedia.h>


// Bytes the root partition can grow into, directly behind it; 0 when the
// root is not a growable disk partition. *reason says why not.
static inline off_t
root_expansion_bytes(BString* reason = NULL)
{
	BString fs;
	if (!root_is_persistent_rw(&fs)) {
		if (reason != NULL)
			*reason = "The system is running from read-only media.";
		return 0;
	}

	BDiskDeviceRoster roster;
	BDiskDevice device;
	BPartition* partition = NULL;
	if (roster.FindPartitionByMountPoint("/", &device, &partition) != B_OK
		|| partition == NULL || partition->Parent() == NULL) {
		if (reason != NULL)
			*reason = "The system partition was not found on a disk.";
		return 0;
	}

	BDiskSystem diskSystem;
	bool whileMounted = false;
	if (partition->GetDiskSystem(&diskSystem) != B_OK
		|| !diskSystem.IsFileSystem()
		|| !diskSystem.SupportsResizing(&whileMounted) || !whileMounted) {
		if (reason != NULL) {
			reason->SetToFormat("The system filesystem (%s) cannot be "
				"grown here.", fs.String());
		}
		return 0;
	}

	BPartitioningInfo info;
	if (partition->Parent()->GetPartitioningInfo(&info) != B_OK)
		return 0;

	off_t end = partition->Offset() + partition->Size();
	for (int32 i = 0; i < info.CountPartitionableSpaces(); i++) {
		off_t offset;
		off_t size;
		if (info.GetPartitionableSpaceAt(i, &offset, &size) != B_OK
			|| offset != end) {
			continue;
		}

		off_t proposed = offset + size - partition->Offset();
		if (partition->ValidateResize(&proposed) == B_OK
			&& proposed > partition->Size()) {
			return proposed - partition->Size();
		}
	}

	if (reason != NULL)
		*reason = "There is no free space directly after the system partition.";
	return 0;
}


#endif	// _ROOT_EXPANSION_H
