/*
 * Copyright 2019-2021, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <Port.h>

#include <map>
#include <string>

#include <dirent.h>
#include <errno.h>
#include <linux/limits.h>
#include <sys/inotify.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "Team.h"
#include "KernelDebug.h"

#include "../kernel/nexus/nexus.h"


static int gNexus = -1;


extern "C" void
init_ports()
{
	gNexus = BKernelPrivate::Team::GetNexusDescriptor();
}


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

	if (ioctl(gNexus, NEXUS_PORT_CREATE, &exchange) < 0)
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

	if (ioctl(gNexus, NEXUS_PORT_OP, &exchange) < 0)
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

	if (ioctl(gNexus, NEXUS_PORT_OP, &exchange) < 0)
		return exchange.return_code;;

	return B_OK;
}


port_id
find_port(const char* name)
{
	//CALLED();

	if (name == NULL)
		return B_BAD_VALUE;

	TRACE("find %s\n", name);

	struct nexus_port_exchange exchange;
	exchange.buffer = name;
	exchange.size = strlen(name)+1;

	if (ioctl(gNexus, NEXUS_PORT_FIND, &exchange) < 0)
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
_get_port_info(port_id id, port_info* info, size_t size)
{
	UNIMPLEMENTED();
	return B_OK;
}


ssize_t
port_count(port_id id)
{
	CALLED();

	if (id < 0)
		return B_BAD_PORT_ID;

	struct nexus_port_exchange exchange;
	exchange.op = NEXUS_PORT_COUNT;
	exchange.id = id;

	if (ioctl(gNexus, NEXUS_PORT_OP, &exchange) < 0)
		return exchange.return_code;

	return exchange.cookie;
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

	TRACE("buffer size %d %u %lu\n", id, flags, timeout);

	port_message_info info;
	memset(&info, 0, sizeof(info));
	status_t ret = _get_port_message_info_etc(id, &info, sizeof(info),
		flags, timeout);

	if (ret == B_OK)
		return info.size;

	return ret;
}


status_t
_get_port_message_info_etc(port_id id, port_message_info* info,
	size_t infoSize, uint32 flags, bigtime_t timeout)
{
	CALLED();

	if (id < 0)
		return B_BAD_PORT_ID;

	if (info == NULL || infoSize != sizeof(*info))
		return B_BAD_VALUE;

	TRACE("read args %s %d %d %u %lu\n", _find_port_name(id),
		find_thread(NULL), id, flags, timeout);

	struct nexus_port_exchange exchange;
	exchange.op = NEXUS_PORT_INFO;
	exchange.id = id;
	exchange.flags = flags;
	exchange.timeout = timeout;

	nexus_port_message_info privateInfo;
	exchange.buffer = &privateInfo;
	exchange.size = sizeof(privateInfo);

	if (ioctl(gNexus, NEXUS_PORT_OP, &exchange) < 0)
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

	if (msgCode == NULL || (msgBuffer == NULL && bufferSize > 0)
			|| bufferSize > PORT_MAX_MESSAGE_SIZE) {
		return B_BAD_VALUE;
	}

	TRACE("read args %s %d %d %u %lu\n", _find_port_name(id),
		find_thread(NULL), id, flags, timeout);

	struct nexus_port_exchange exchange;
	exchange.op = NEXUS_PORT_READ;
	exchange.id = id;
	exchange.code = msgCode;
	exchange.buffer = msgBuffer;
	exchange.size = bufferSize;
	exchange.flags = flags;
	exchange.timeout = timeout;

	if (ioctl(gNexus, NEXUS_PORT_OP, &exchange) < 0)
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

	TRACE("write args %s %d %d %u %lu\n", _find_port_name(id),
		find_thread(NULL), id, flags, timeout);

	struct nexus_port_exchange exchange;
	exchange.op = NEXUS_PORT_WRITE;
	exchange.id = id;
	exchange.code = &msgCode;
	exchange.buffer = msgBuffer;
	exchange.size = bufferSize;
	exchange.flags = flags;
	exchange.timeout = timeout;

	if (ioctl(gNexus, NEXUS_PORT_OP, &exchange) < 0)
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
	UNIMPLEMENTED();

	// TODO: we want to deprecate this function
	// and introduce a mechanism which requires
	// the target process approval.

	if (id < 0)
		return B_BAD_PORT_ID;

	//struct nexus_port_exchange exchange;
	//exchange.op = NEXUS_PORT_TRANSFER;
	//exchange.id = id;

	// TODO: fill

	//if (ioctl(gNexus, NEXUS_PORT_OP, &exchange) < 0)
	//	return B_BAD_PORT_ID;

	return B_OK;
}


extern "C"
int
dump_port_info(int argc, char **argv)
{
}
