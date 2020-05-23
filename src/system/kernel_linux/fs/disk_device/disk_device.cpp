/*
 *  Copyright 2019-2020, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include <syscalls.h>


int
_kern_get_next_disk_device_id(int32* cookie, size_t* neededSize)
{
	UNIMPLEMENTED();
	return -1;
}


partition_id
_kern_find_disk_device(const char* filename, size_t* neededSize)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


partition_id
_kern_find_partition(const char* filename, size_t* neededSize)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


partition_id
_kern_find_file_disk_device(const char* filename, size_t* neededSize)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_get_disk_device_data(partition_id deviceID, bool deviceOnly,
	struct user_disk_device_data* buffer,
	size_t bufferSize, size_t* neededSize)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


partition_id
_kern_register_file_device(const char* filename)
{
	UNIMPLEMENTED();
	return -1;
}


status_t
_kern_unregister_file_device(partition_id deviceID,
	const char* filename)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_get_file_disk_device_path(partition_id id,
	char* buffer, size_t bufferSize)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_get_disk_system_info(disk_system_id id,
	struct user_disk_system_info* info)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_get_next_disk_system_info(int32* cookie,
	struct user_disk_system_info* info)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_find_disk_system(const char* name, struct user_disk_system_info* info)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_defragment_partition(partition_id partitionID, int32* changeCounter)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_repair_partition(partition_id partitionID,
	int32* changeCounter, bool checkOnly)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_resize_partition(partition_id partitionID, int32* changeCounter,
	partition_id childID,int32* childChangeCounter, off_t size,
	off_t contentSize)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_move_partition(partition_id partitionID, int32* changeCounter,
	partition_id childID, int32* childChangeCounter, off_t newOffset,
	partition_id* descendantIDs, int32* descendantChangeCounters,
	int32 descendantCount)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


status_t
_kern_set_partition_name(partition_id partitionID, int32* changeCounter,
	partition_id childID, int32* childChangeCounter, const char* name)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_set_partition_content_name(partition_id partitionID,
	int32* changeCounter, const char* name)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_set_partition_type(partition_id partitionID, int32* changeCounter,
	partition_id childID, int32* childChangeCounter, const char* type)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_set_partition_parameters(partition_id partitionID, int32* changeCounter,
	partition_id childID, int32* childChangeCounter, const char* parameters)
{
	UNIMPLEMENTED();
	return B_ERROR;
}



status_t
_kern_set_partition_content_parameters(partition_id partitionID,
	int32* changeCounter, const char* parameters)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_initialize_partition(partition_id partitionID, int32* changeCounter,
	const char* diskSystemName, const char* name, const char* parameters)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_uninitialize_partition(partition_id partitionID, int32* changeCounter,
	partition_id parentID, int32* parentChangeCounter)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_create_child_partition(partition_id partitionID, int32* changeCounter,
	off_t offset, off_t size, const char* type, const char* name,
	const char* parameters, partition_id* childID,
	int32* childChangeCounter)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_delete_child_partition(partition_id partitionID, int32* changeCounter,
	partition_id childID, int32 childChangeCounter)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_start_watching_disks(uint32 eventMask, port_id port, int32 token)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_stop_watching_disks(port_id port, int32 token)
{
	UNIMPLEMENTED();
	return B_ERROR;
}
