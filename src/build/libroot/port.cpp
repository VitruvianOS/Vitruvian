/*
 * Copyright 2019-2023, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <OS.h>

port_id	
create_port(int32 queueLength, const char* name)
{
	return B_ERROR;
}


status_t
close_port(port_id id)
{
	return B_ERROR;
}


status_t
delete_port(port_id id)
{
	return B_ERROR;
}


port_id
find_port(const char* name)
{
	return B_ERROR;
}


status_t
_kern_get_next_port_info(team_id team, int32* _cookie,
	struct port_info* info)
{
	return B_ERROR;
}


status_t
_get_port_info(port_id id, port_info* out_info, size_t size)
{
	return B_ERROR;
}


ssize_t
port_count(port_id id)
{
	return B_ERROR;
}


ssize_t
port_buffer_size(port_id id)
{
	return B_ERROR;
}


ssize_t
port_buffer_size_etc(port_id id, uint32 flags, bigtime_t timeout)
{
	return B_ERROR;
}


status_t
_get_port_message_info_etc(port_id id, port_message_info* info,
	size_t infoSize, uint32 flags, bigtime_t timeout)
{
	return B_ERROR;
}


ssize_t
read_port_etc(port_id id, int32* msgCode, void* msgBuffer,
	size_t bufferSize, uint32 flags, bigtime_t timeout)
{
	return B_ERROR;
}


ssize_t
read_port(port_id port, int32* msgCode,
	void* msgBuffer, size_t bufferSize)
{
	return B_ERROR;
}


status_t
write_port_etc(port_id id, int32 msgCode, const void* msgBuffer,
	size_t bufferSize, uint32 flags, bigtime_t timeout)
{
	return B_ERROR;
}


status_t
write_port(port_id id, int32 msgCode,
	const void* msgBuffer, size_t bufferSize)
{
	return B_ERROR;
}


status_t
set_port_owner(port_id id, team_id team)
{
	return B_ERROR;
}
