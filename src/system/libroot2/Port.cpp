/*
 * Copyright 2019-2023, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "Team.h"
#include "KernelDebug.h"

#include "../kernel/nexus/nexus/nexus.h"


port_id	
create_port(int32 queueLength, const char* name)
{
	CALLED();

	if (queueLength < 1 || queueLength > PORT_MAX_QUEUE || name == NULL)
		return B_BAD_VALUE;

	struct nexus_port_exchange exchange;
	exchange.buffer = name;
	exchange.size = strlen(name)+1;
	exchange.cookie = queueLength;

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	if (ioctl(nexus, NEXUS_PORT_CREATE, &exchange) < 0)
		return exchange.return_code;

	return exchange.id;
}


status_t
close_port(port_id id)
{
	CALLED();

	if (id < 0)
		return B_BAD_PORT_ID;

	struct nexus_port_exchange exchange;
	exchange.op = NEXUS_PORT_CLOSE;
	exchange.id = id;

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	if (ioctl(nexus, NEXUS_PORT_OP, &exchange) < 0)
		return exchange.return_code;

	return B_OK;
}


status_t
delete_port(port_id id)
{
	CALLED();

	if (id < 0)
		return B_BAD_PORT_ID;

	struct nexus_port_exchange exchange;
	exchange.op = NEXUS_PORT_DELETE;
	exchange.id = id;

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	if (ioctl(nexus, NEXUS_PORT_OP, &exchange) < 0)
		return exchange.return_code;

	return B_OK;
}


port_id
find_port(const char* name)
{
	if (name == NULL)
		return B_BAD_VALUE;

	struct nexus_port_exchange exchange;
	exchange.buffer = name;
	exchange.size = strlen(name)+1;

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	if (ioctl(nexus, NEXUS_PORT_FIND, &exchange) < 0)
		return exchange.return_code;

	return exchange.id;
}


status_t
_kern_get_next_port_info(team_id team, int32* _cookie,
	struct port_info* info)
{
	UNIMPLEMENTED();
	// Maybe this could be read from proc
	return B_ERROR;
}


status_t
_get_port_info(port_id id, port_info* out_info, size_t size)
{
	CALLED();

	if (id < 0)
		return B_BAD_PORT_ID;

	if (out_info == NULL || size != sizeof(*out_info))
		return B_BAD_VALUE;

	struct nexus_port_exchange exchange;
	nexus_port_info info;

	exchange.op = NEXUS_PORT_INFO;
	exchange.id = id;
	exchange.buffer = (void*)&info;

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	if (ioctl(nexus, NEXUS_PORT_OP, &exchange) < 0)
		return exchange.return_code;

	out_info->port = info.port;
	out_info->team = info.team;
	out_info->capacity = info.capacity;
	out_info->queue_count = info.queue_count;
	out_info->total_count = info.total_count;

	return B_OK;
}


ssize_t
port_count(port_id id)
{
	CALLED();

	if (id < 0)
		return B_BAD_PORT_ID;

	port_info info;
	status_t ret = get_port_info(id, &info);

	return ret != B_OK ? ret : info.queue_count;
}


// block
ssize_t
port_buffer_size(port_id id)
{
	CALLED();
	return port_buffer_size_etc(id, 0, 0);
}


// block or timeout
ssize_t
port_buffer_size_etc(port_id id, uint32 flags, bigtime_t timeout)
{
	CALLED();

	if (id < 0)
		return B_BAD_PORT_ID;

	port_message_info info;
	memset(&info, 0, sizeof(info));

	status_t ret = _get_port_message_info_etc(id, &info, sizeof(info),
		flags, timeout);

	return (ret == B_OK) ? info.size : ret;
}


status_t
_get_port_message_info_etc(port_id id, port_message_info* info,
	size_t infoSize, uint32 flags, bigtime_t timeout)
{
	CALLED();

	if (id < 0)
		return B_BAD_PORT_ID;

	if (info == NULL /*|| infoSize != sizeof(*info)*/)
		return B_BAD_VALUE;

	struct nexus_port_exchange exchange;
	exchange.op = NEXUS_PORT_MESSAGE_INFO;
	exchange.id = id;
	exchange.flags = flags;
	exchange.timeout = timeout;

	nexus_port_message_info privateInfo;
	exchange.buffer = &privateInfo;
	exchange.size = sizeof(privateInfo);

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	if (ioctl(nexus, NEXUS_PORT_OP, &exchange) < 0)
		return exchange.return_code;

	info->size = privateInfo.size;
	info->sender = privateInfo.sender;
	info->sender_group = privateInfo.sender_group;
	info->sender_team = privateInfo.sender_team;

	return B_OK;
};


ssize_t
read_port_etc(port_id id, int32* msgCode, void* msgBuffer,
	size_t bufferSize, uint32 flags, bigtime_t timeout)
{
	CALLED();

	if (id < 0)
		return B_BAD_PORT_ID;

	if ((msgBuffer == NULL && bufferSize > 0)
			|| bufferSize > PORT_MAX_MESSAGE_SIZE) {
		return B_BAD_VALUE;
	}
	struct nexus_port_exchange exchange;
	exchange.op = NEXUS_PORT_READ;
	exchange.id = id;
	exchange.code = msgCode;
	exchange.buffer = msgBuffer;
	exchange.size = bufferSize;
	exchange.flags = flags;
	exchange.timeout = timeout;

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	if (ioctl(nexus, NEXUS_PORT_OP, &exchange) < 0)
		return exchange.return_code;

	return exchange.size;
}


ssize_t
read_port(port_id port, int32* msgCode,
	void* msgBuffer, size_t bufferSize)
{
	return read_port_etc(port, msgCode, msgBuffer, bufferSize, 0, 0);
}


status_t
write_port_etc(port_id id, int32 msgCode, const void* msgBuffer,
	size_t bufferSize, uint32 flags, bigtime_t timeout)
{
	CALLED();

	if (id < 0)
		return B_BAD_PORT_ID;

	if ((msgBuffer == NULL && bufferSize > 0)
			|| bufferSize > PORT_MAX_MESSAGE_SIZE) {
		return B_BAD_VALUE;
	}

	struct nexus_port_exchange exchange;
	exchange.op = NEXUS_PORT_WRITE;
	exchange.id = id;
	exchange.code = &msgCode;
	exchange.buffer = msgBuffer;
	exchange.size = bufferSize;
	exchange.flags = flags;
	exchange.timeout = timeout;

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	if (ioctl(nexus, NEXUS_PORT_OP, &exchange) < 0)
		return exchange.return_code;

	return B_OK;
}


status_t
write_port(port_id id, int32 msgCode,
	const void* msgBuffer, size_t bufferSize)
{
	CALLED();
	return write_port_etc(id, msgCode, msgBuffer, bufferSize, 0, 0);
}


status_t
set_port_owner(port_id id, team_id team)
{
	CALLED();

	// TODO: we want to deprecate this function
	// and introduce a mechanism which requires
	// the target process approval.

	if (id < 0)
		return B_BAD_PORT_ID;


	struct nexus_port_exchange exchange;
	exchange.op = NEXUS_SET_PORT_OWNER;
	exchange.id = id;
	exchange.cookie = team;

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	if (ioctl(nexus, NEXUS_PORT_OP, &exchange) < 0)
		return B_BAD_PORT_ID;

	return exchange.return_code;
}


extern "C"
int
dump_port_info(int argc, char **argv)
{
	// TODO this might be a good candidate for removal.
}
