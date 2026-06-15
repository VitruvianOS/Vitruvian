/*
 * Copyright 2019-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "Team.h"
#include "KernelDebug.h"

#include "../kernel/nexus/nexus/nexus.h"


port_id
create_port(int32 queueLength, const char* name)
{
	CALLED();

	if (name == NULL || queueLength < 1
			|| queueLength > PORT_MAX_QUEUE) {
		return B_BAD_VALUE;
	}

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	if (nexus < 0)
		return B_BAD_PORT_ID;

	struct nexus_port_create exchange;
	memset(&exchange, 0, sizeof(exchange));
	exchange.name = name;
	exchange.size = (uint32_t)(strlen(name) + 1);
	exchange.capacity = queueLength;

	if (nexus_io(nexus, NEXUS_PORT_CREATE, &exchange) < 0)
		return B_ERROR;
	if (exchange.ret != B_OK)
		return exchange.ret;

	return exchange.id;
}


status_t
close_port(port_id id)
{
	CALLED();

	if (id < 0)
		return B_BAD_PORT_ID;

	struct nexus_port_id exchange;
	memset(&exchange, 0, sizeof(exchange));
	exchange.id = id;

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	if (nexus < 0)
		return B_BAD_PORT_ID;

	if (nexus_io(nexus, NEXUS_PORT_CLOSE, &exchange) < 0)
		return B_ERROR;
	return exchange.ret;
}


status_t
delete_port(port_id id)
{
	CALLED();

	if (id < 0)
		return B_BAD_PORT_ID;

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	if (nexus < 0)
		return B_BAD_PORT_ID;

	struct nexus_port_id exchange;
	memset(&exchange, 0, sizeof(exchange));
	exchange.id = id;

	if (nexus_io(nexus, NEXUS_PORT_DELETE, &exchange) < 0)
		return B_ERROR;
	return exchange.ret;
}


port_id
find_port(const char* name)
{
	if (name == NULL || name[0] == '\0')
		return B_BAD_VALUE;

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	if (nexus < 0)
		return B_BAD_PORT_ID;

	struct nexus_port_find_req exchange;
	memset(&exchange, 0, sizeof(exchange));
	exchange.name = name;
	exchange.size = strlen(name)+1;

	if (nexus_io(nexus, NEXUS_PORT_FIND, &exchange) < 0)
		return B_ERROR;
	if (exchange.ret != B_OK)
		return exchange.ret;

	return exchange.id;
}


status_t
_kern_get_next_port_info(team_id team, int32* _cookie,
	struct port_info* info)
{
	if (_cookie == NULL || info == NULL)
		return B_BAD_VALUE;

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	if (nexus < 0)
		return B_BAD_PORT_ID;

	struct nexus_get_next_port req;
	memset(&req, 0, sizeof(req));
	req.team   = (pid_t)team;
	req.cookie = *_cookie;

	if (nexus_io(nexus, NEXUS_GET_NEXT_PORT_FOR_TEAM, &req) < 0)
		return B_ERROR;
	if (req.ret != B_OK)
		return req.ret;

	info->port        = req.info.port;
	info->team        = req.info.team;
	info->capacity    = req.info.capacity;
	info->queue_count = req.info.queue_count;
	info->total_count = req.info.total_count;
	strlcpy(info->name, req.info.name, sizeof(info->name));

	// Advance cookie to the port id we just returned
	*_cookie = req.info.port;
	return B_OK;
}


status_t
_get_next_port_info(team_id team, int32* cookie, port_info* info, size_t size)
{
	return _kern_get_next_port_info(team, cookie, info);
}


status_t
_get_port_info(port_id id, port_info* out_info, size_t size)
{
	CALLED();

	if (id < 0)
		return B_BAD_PORT_ID;

	if (out_info == NULL || size != sizeof(*out_info))
		return B_BAD_VALUE;

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	if (nexus < 0)
		return B_BAD_PORT_ID;

	struct nexus_port_get_info exchange;
	struct nexus_port_info info;
	memset(&info, 0, sizeof(info));

	exchange.id = id;
	exchange.info = &info;

	if (nexus_io(nexus, NEXUS_PORT_INFO, &exchange) < 0)
		return B_ERROR;
	if (exchange.ret != B_OK)
		return exchange.ret;

	out_info->port = info.port;
	out_info->team = info.team;
	strlcpy(out_info->name, info.name, sizeof(out_info->name));
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

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	if (nexus < 0)
		return B_BAD_PORT_ID;

	struct nexus_port_get_message_info exchange;
	struct nexus_port_message_info privateInfo;
	memset(&privateInfo, 0, sizeof(privateInfo));

	exchange.id = id;
	exchange.flags = flags;
	exchange.timeout = timeout;
	exchange.size = sizeof(privateInfo);
	exchange.info = &privateInfo;

	if (nexus_io(nexus, NEXUS_PORT_MESSAGE_INFO, &exchange) < 0)
		return B_ERROR;
	if (exchange.ret != B_OK)
		return exchange.ret;

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

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	if (nexus < 0)
		return B_BAD_PORT_ID;

	struct nexus_port_read exchange;
	memset(&exchange, 0, sizeof(exchange));
	exchange.id = id;
	exchange.code = msgCode;
	exchange.buffer = msgBuffer;
	exchange.size = bufferSize;
	exchange.flags = flags;
	exchange.timeout = timeout;

	if (nexus_io(nexus, NEXUS_PORT_READ, &exchange) < 0)
		return B_ERROR;
	if (exchange.ret != B_OK)
		return exchange.ret;

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

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	if (nexus < 0)
		return B_BAD_PORT_ID;

	struct nexus_port_write exchange;
	memset(&exchange, 0, sizeof(exchange));
	exchange.id = id;
	exchange.code = &msgCode;
	exchange.buffer = msgBuffer;
	exchange.size = bufferSize;
	exchange.flags = flags;
	exchange.timeout = timeout;

	if (nexus_io(nexus, NEXUS_PORT_WRITE, &exchange) < 0)
		return B_ERROR;
	return exchange.ret;
}


status_t
write_port(port_id id, int32 msgCode,
	const void* msgBuffer, size_t bufferSize)
{
	CALLED();
	return write_port_etc(id, msgCode, msgBuffer, bufferSize, 0, 0);
}


status_t
write_port_with_caps(port_id id, int32 msgCode,
	const void* msgBuffer, size_t bufferSize,
	const port_cap_in* caps, size_t capsCount,
	uint32 flags, bigtime_t timeout)
{
	CALLED();

	if (id < 0)
		return B_BAD_PORT_ID;

	if ((msgBuffer == NULL && bufferSize > 0)
			|| bufferSize > PORT_MAX_MESSAGE_SIZE) {
		return B_BAD_VALUE;
	}

	if (caps == NULL && capsCount > 0)
		return B_BAD_VALUE;

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	if (nexus < 0)
		return B_BAD_PORT_ID;

	// Userland `port_cap_in` matches `nexus_port_cap_in` byte-for-byte.
	struct nexus_port_write_caps exchange;
	memset(&exchange, 0, sizeof(exchange));
	exchange.id = id;
	exchange.code = &msgCode;
	exchange.buffer = msgBuffer;
	exchange.size = bufferSize;
	exchange.caps = (const struct nexus_port_cap_in*)caps;
	exchange.caps_count = capsCount;
	exchange.flags = flags;
	exchange.timeout = timeout;

	if (nexus_io(nexus, NEXUS_PORT_WRITE_CAPS, &exchange) < 0)
		return B_ERROR;
	return exchange.ret;
}


ssize_t
read_port_with_caps(port_id id, int32* msgCode,
	void* msgBuffer, size_t* bufferSize,
	port_cap_out* caps, size_t* capsCount,
	uint32 flags, bigtime_t timeout)
{
	CALLED();

	if (id < 0)
		return B_BAD_PORT_ID;

	if (bufferSize == NULL || capsCount == NULL)
		return B_BAD_VALUE;

	if ((msgBuffer == NULL && *bufferSize > 0)
			|| *bufferSize > PORT_MAX_MESSAGE_SIZE) {
		return B_BAD_VALUE;
	}

	if (caps == NULL && *capsCount > 0)
		return B_BAD_VALUE;

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	if (nexus < 0)
		return B_BAD_PORT_ID;

	// Userland `port_cap_out` matches `nexus_port_cap_out` byte-for-byte.
	struct nexus_port_read_caps exchange;
	memset(&exchange, 0, sizeof(exchange));
	exchange.id = id;
	exchange.code = msgCode;
	exchange.buffer = msgBuffer;
	exchange.size = *bufferSize;
	exchange.caps = (struct nexus_port_cap_out*)caps;
	exchange.caps_count = *capsCount;
	exchange.flags = flags;
	exchange.timeout = timeout;

	if (nexus_io(nexus, NEXUS_PORT_READ_CAPS, &exchange) < 0)
		return B_ERROR;

	// Kernel writes back actual size and caps_count, even on overflow,
	// so the caller can re-size and retry.
	*bufferSize = exchange.size;
	*capsCount = exchange.caps_count;

	if (exchange.ret != B_OK)
		return exchange.ret;

	return (ssize_t)exchange.size;
}


status_t
set_port_owner(port_id id, team_id team)
{
	CALLED();

	// TODO: we want to deprecate this function
	// and introduce a mechanism that requires
	// the target process approval.

	if (id < 0)
		return B_BAD_PORT_ID;

	if (team < 0)
		return B_BAD_TEAM_ID;

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	if (nexus < 0)
		return B_BAD_PORT_ID;

	struct nexus_port_set_owner exchange;
	memset(&exchange, 0, sizeof(exchange));
	exchange.id = id;
	exchange.team = team;

	if (nexus_io(nexus, NEXUS_SET_PORT_OWNER, &exchange) < 0)
		return B_ERROR;
	return exchange.ret;
}
