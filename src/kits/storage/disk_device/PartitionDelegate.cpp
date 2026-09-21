/*
 * Copyright 2007, Ingo Weinhold, bonefish@users.sf.net.
 * Distributed under the terms of the MIT License.
 */

#include "PartitionDelegate.h"

#include <stdio.h>
#include <string.h>

#include <DiskDeviceTypes.h>
#include <PartitioningInfo.h>

#include <ddm_userland_interface_defs.h>
#include <syscalls.h>

//#define TRACE_PARTITION_DELEGATE
#undef TRACE
#ifdef TRACE_PARTITION_DELEGATE
# define TRACE(x...) printf(x)
#else
# define TRACE(x...) do {} while (false)
#endif


// looks up gDiskSystems (libroot2); the sole capability source
static status_t
lookup_disk_system(const char* name, uint32* _flags)
{
	if (name == NULL)
		return B_ENTRY_NOT_FOUND;

	user_disk_system_info info;
	status_t error = _kern_find_disk_system(name, &info);
	if (error != B_OK)
		return error;

	*_flags = info.flags;
	return B_OK;
}


// constructor
BPartition::Delegate::Delegate(BPartition* partition)
	:
	fPartition(partition),
	fMutablePartition(this)
{
}


// destructor
BPartition::Delegate::~Delegate()
{
}


// MutablePartition
BMutablePartition*
BPartition::Delegate::MutablePartition()
{
	return &fMutablePartition;
}


// MutablePartition
const BMutablePartition*
BPartition::Delegate::MutablePartition() const
{
	return &fMutablePartition;
}


// InitHierarchy
status_t
BPartition::Delegate::InitHierarchy(
	const user_partition_data* partitionData, Delegate* parent)
{
	return fMutablePartition.Init(partitionData,
		parent ? &parent->fMutablePartition : NULL);
}


// InitAfterHierarchy
status_t
BPartition::Delegate::InitAfterHierarchy()
{
	// nothing to do; capabilities are looked up on demand
	return B_OK;
}


// PartitionData
const user_partition_data*
BPartition::Delegate::PartitionData() const
{
	return fMutablePartition.PartitionData();
}


// ChildAt
BPartition::Delegate*
BPartition::Delegate::ChildAt(int32 index) const
{
	BMutablePartition* child = fMutablePartition.ChildAt(index);
	return child ? child->GetDelegate() : NULL;
}


// CountChildren
int32
BPartition::Delegate::CountChildren() const
{
	return fMutablePartition.CountChildren();
}


// IsModified
bool
BPartition::Delegate::IsModified() const
{
	return fMutablePartition.ChangeFlags() != 0;
}


// SupportedOperations
uint32
BPartition::Delegate::SupportedOperations(uint32 mask)
{
	uint32 flags;
	if (lookup_disk_system(fMutablePartition.ContentType(), &flags) != B_OK)
		return 0;

	return flags & mask;
}


// SupportedChildOperations
uint32
BPartition::Delegate::SupportedChildOperations(Delegate* /*child*/,
	uint32 mask)
{
	// child capabilities depend on this content type, not the child
	uint32 flags;
	if (lookup_disk_system(fMutablePartition.ContentType(), &flags) != B_OK)
		return 0;

	return flags & mask;
}


// Defragment
status_t
BPartition::Delegate::Defragment()
{
// TODO: Implement!
	return B_BAD_VALUE;
}


// Repair
status_t
BPartition::Delegate::Repair(bool /*checkOnly*/)
{
	return B_NOT_SUPPORTED;
}


// ValidateResize
status_t
BPartition::Delegate::ValidateResize(off_t* size) const
{
	if (size == NULL)
		return B_BAD_VALUE;

	return B_OK;
}


// ValidateResizeChild
status_t
BPartition::Delegate::ValidateResizeChild(Delegate* child, off_t* size) const
{
	if (child == NULL || size == NULL)
		return B_NO_INIT;

	return B_OK;
}


// Resize
status_t
BPartition::Delegate::Resize(off_t size)
{
	fMutablePartition.SetContentSize(size);
	return B_OK;
}


// ResizeChild
status_t
BPartition::Delegate::ResizeChild(Delegate* child, off_t size)
{
	if (child == NULL)
		return B_NO_INIT;

	child->fMutablePartition.SetSize(size);
	return B_OK;
}


// ValidateMove
status_t
BPartition::Delegate::ValidateMove(off_t* offset) const
{
	if (offset == NULL)
		return B_BAD_VALUE;

	return B_OK;
}


// ValidateMoveChild
status_t
BPartition::Delegate::ValidateMoveChild(Delegate* child, off_t* offset) const
{
	if (child == NULL || offset == NULL)
		return B_NO_INIT;

	return B_OK;
}


// Move
status_t
BPartition::Delegate::Move(off_t /*offset*/)
{
	// content offset is not tracked separately; see MoveChild()
	return B_OK;
}


// MoveChild
status_t
BPartition::Delegate::MoveChild(Delegate* child, off_t offset)
{
	if (child == NULL)
		return B_NO_INIT;

	child->fMutablePartition.SetOffset(offset);
	return B_OK;
}


// ValidateSetContentName
status_t
BPartition::Delegate::ValidateSetContentName(BString* name) const
{
	if (name == NULL)
		return B_BAD_VALUE;

	return B_OK;
}


// ValidateSetName
status_t
BPartition::Delegate::ValidateSetName(Delegate* child, BString* name) const
{
	if (child == NULL || name == NULL)
		return B_BAD_VALUE;

	return B_OK;
}


// SetContentName
status_t
BPartition::Delegate::SetContentName(const char* name)
{
	return fMutablePartition.SetContentName(name);
}


// SetName
status_t
BPartition::Delegate::SetName(Delegate* child, const char* name)
{
	if (child == NULL)
		return B_BAD_VALUE;

	return child->fMutablePartition.SetName(name);
}


// ValidateSetType
status_t
BPartition::Delegate::ValidateSetType(Delegate* child, const char* type) const
{
	if (child == NULL || type == NULL)
		return B_BAD_VALUE;

	return B_OK;
}


// SetType
status_t
BPartition::Delegate::SetType(Delegate* child, const char* type)
{
	if (child == NULL)
		return B_BAD_VALUE;

	return child->fMutablePartition.SetType(type);
}


// SetContentParameters
status_t
BPartition::Delegate::SetContentParameters(const char* parameters)
{
	return fMutablePartition.SetContentParameters(parameters);
}


// SetParameters
status_t
BPartition::Delegate::SetParameters(Delegate* child, const char* parameters)
{
	if (child == NULL)
		return B_BAD_VALUE;

	return child->fMutablePartition.SetParameters(parameters);
}


// GetNextSupportedChildType
status_t
BPartition::Delegate::GetNextSupportedChildType(Delegate* /*child*/,
	int32* cookie, BString* type) const
{
	if (cookie == NULL || type == NULL)
		return B_BAD_VALUE;

	// keep in step with the type vocabulary in vos-partition-lib.sh
	static const char* const kChildTypes[] = {
		"linux",
		"esp",
		"linux_swap",
		"bios_boot"
	};
	static const int32 kChildTypeCount
		= (int32)(sizeof(kChildTypes) / sizeof(kChildTypes[0]));

	const char* contentType = fMutablePartition.ContentType();
	if (contentType == NULL)
		return B_ENTRY_NOT_FOUND;

	bool isIntel = strcmp(contentType, kPartitionTypeIntel) == 0
		|| strcmp(contentType, "intel") == 0;
	bool isEFI = strcmp(contentType, kPartitionTypeEFI) == 0
		|| strcmp(contentType, "gpt") == 0;
	if (!isIntel && !isEFI)
		return B_ENTRY_NOT_FOUND;

	// bios_boot is GPT-only (BIOS GRUB); not offered on dos
	int32 count = isEFI ? kChildTypeCount : kChildTypeCount - 1;

	if (*cookie < 0 || *cookie >= count)
		return B_ENTRY_NOT_FOUND;

	type->SetTo(kChildTypes[*cookie]);
	(*cookie)++;

	return B_OK;
}


// IsSubSystem
bool
BPartition::Delegate::IsSubSystem(Delegate* /*child*/,
	const char* /*diskSystem*/) const
{
	return false;
}


// CanInitialize
bool
BPartition::Delegate::CanInitialize(const char* diskSystem) const
{
	uint32 flags;
	if (lookup_disk_system(diskSystem, &flags) != B_OK)
		return false;

	if ((flags & B_DISK_SYSTEM_IS_FILE_SYSTEM) != 0)
		return (flags & B_DISK_SYSTEM_SUPPORTS_WRITING) != 0;

	return true;
}


// ValidateInitialize
status_t
BPartition::Delegate::ValidateInitialize(const char* diskSystem,
	BString* /*name*/, const char* /*parameters*/)
{
	if (!CanInitialize(diskSystem))
		return B_NOT_SUPPORTED;

	return B_OK;
}


// Initialize
status_t
BPartition::Delegate::Initialize(const char* diskSystem,
	const char* name, const char* parameters)
{
	user_disk_system_info info;
	status_t error = _kern_find_disk_system(diskSystem, &info);
	if (error != B_OK)
		return error;

	Uninitialize();

	error = fMutablePartition.SetContentType(info.name);
	if (error == B_OK)
		error = fMutablePartition.SetContentName(name);
	if (error == B_OK)
		error = fMutablePartition.SetContentParameters(parameters);
	if (error != B_OK)
		return error;

	fMutablePartition.ClearFlags(B_PARTITION_FILE_SYSTEM
		| B_PARTITION_PARTITIONING_SYSTEM);
	fMutablePartition.SetFlags(fMutablePartition.Flags()
		| ((info.flags & B_DISK_SYSTEM_IS_FILE_SYSTEM) != 0
			? B_PARTITION_FILE_SYSTEM : B_PARTITION_PARTITIONING_SYSTEM));
	fMutablePartition.SetStatus(B_PARTITION_VALID);

	return B_OK;
}


// Uninitialize
status_t
BPartition::Delegate::Uninitialize()
{
	if (fMutablePartition.ContentType() != NULL)
		fMutablePartition.UninitializeContents();

	return B_OK;
}


// get_partitioning_system_reserved_space
/*!	Mirrors the front/back space the Haiku intel/gpt add-ons reserve. */
static void
get_partitioning_system_reserved_space(const char* contentType,
	off_t blockSize, off_t* _frontReserved, off_t* _backReserved)
{
	*_frontReserved = 0;
	*_backReserved = 0;

	if (contentType == NULL || blockSize <= 0)
		return;

	// match both spellings: gDiskSystems stores "intel"/"gpt" short names
	if (strcmp(contentType, kPartitionTypeIntel) == 0
		|| strcmp(contentType, "intel") == 0) {
		*_frontReserved = 64 * blockSize;
	} else if (strcmp(contentType, kPartitionTypeEFI) == 0
		|| strcmp(contentType, "gpt") == 0) {
		off_t entryArrayBlocks = (128 * 128 + blockSize - 1) / blockSize;
		*_frontReserved = (2 + entryArrayBlocks) * blockSize;
		*_backReserved = (1 + entryArrayBlocks) * blockSize;
	}
}


// GetPartitioningInfo
status_t
BPartition::Delegate::GetPartitioningInfo(BPartitioningInfo* info)
{
	if (info == NULL)
		return B_BAD_VALUE;

	// only partitioning systems have partitionable space
	if ((fMutablePartition.Flags() & B_PARTITION_PARTITIONING_SYSTEM) == 0)
		return info->SetTo(0, 0);

	off_t offset = fMutablePartition.Offset();
	off_t size = fMutablePartition.Size();

	status_t error = info->SetTo(offset, size);
	if (error != B_OK)
		return error;

	off_t frontReserved = 0;
	off_t backReserved = 0;
	get_partitioning_system_reserved_space(fMutablePartition.ContentType(),
		fMutablePartition.BlockSize(), &frontReserved, &backReserved);

	if (frontReserved > 0) {
		error = info->ExcludeOccupiedSpace(offset, frontReserved);
		if (error != B_OK)
			return error;
	}

	if (backReserved > 0) {
		error = info->ExcludeOccupiedSpace(offset + size - backReserved,
			backReserved);
		if (error != B_OK)
			return error;
	}

	for (int32 i = 0; i < fMutablePartition.CountChildren(); i++) {
		BMutablePartition* child = fMutablePartition.ChildAt(i);
		error = info->ExcludeOccupiedSpace(child->Offset(), child->Size());
		if (error != B_OK)
			return error;
	}

	return B_OK;
}


// GetParameterEditor
status_t
BPartition::Delegate::GetParameterEditor(B_PARAMETER_EDITOR_TYPE /*type*/,
	BPartitionParameterEditor** /*editor*/) const
{
	// The add-on ABI this served (a shared-object supplied BView) is gone.
	return B_NOT_SUPPORTED;
}


// ValidateCreateChild
status_t
BPartition::Delegate::ValidateCreateChild(off_t* start, off_t* size,
	const char* /*type*/, BString* /*name*/, const char* /*parameters*/) const
{
	if (start == NULL || size == NULL || *start < 0 || *size <= 0)
		return B_BAD_VALUE;

	return B_OK;
}


// CreateChild
status_t
BPartition::Delegate::CreateChild(off_t start, off_t size, const char* type,
	const char* name, const char* parameters, BPartition** child)
{
	BMutablePartition* mutableChild;
	status_t error = fMutablePartition.CreateChild(-1, type, name, parameters,
		&mutableChild);
	if (error != B_OK)
		return error;

	mutableChild->SetOffset(start);
	mutableChild->SetSize(size);

	if (child)
		*child = mutableChild->GetDelegate()->Partition();

	return B_OK;
}


// DeleteChild
status_t
BPartition::Delegate::DeleteChild(Delegate* child)
{
	if (child == NULL)
		return B_NO_INIT;

	return fMutablePartition.DeleteChild(&child->fMutablePartition);
}
