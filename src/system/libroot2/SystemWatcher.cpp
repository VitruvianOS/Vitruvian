/*
 *  Copyright 2019-2020, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include "SystemWatcher.h"

#include <syscalls.h>

#include <linux/connector.h>
#include <stdio.h>
#include <stdlib.h>

#include "system_info.h"
#include "KernelDebug.h"
#include "util/AutoLock.h"
#include "util/KMessage.h"


namespace BKernelPrivate {


SystemWatcher* SystemWatcher::fInstance = NULL;


SystemWatcher::SystemWatcher()
	:
	fRunning(false),
	fSocket(0),
	fBuf(NULL)
{
}


bool
SystemWatcher::IsRunning() const
{
	return fRunning;
}


status_t
SystemWatcher::Run()
{
	CALLED();

	fSocket = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
	if (fSocket == -1) {
		TRACE("SystemWatcher: Socket error");
		return B_ERROR;
	}

	fAddr.nl_family = AF_NETLINK;
	fAddr.nl_pid = getpid();
	fAddr.nl_groups = CN_IDX_PROC;

	if (bind(fSocket, (struct sockaddr*)&fAddr, sizeof(fAddr))) {
		TRACE("SystemWatcher: Bind error");
		close(fSocket);
		return B_ERROR;
	}

	struct
	{
		union {
			struct nlmsghdr header;
			char padding[NLMSG_HDRLEN];
		};

		enum proc_cn_mcast_op op;
		struct cn_msg msg;
	} __attribute__((packed, aligned(NLMSG_ALIGNTO))) request;

    memset(&request, 0, sizeof(request));

    request.op = PROC_CN_MCAST_LISTEN;

    request.msg.id.idx = CN_IDX_PROC;
    request.msg.id.val = CN_VAL_PROC;
    request.msg.len = sizeof(request.op);

    request.header.nlmsg_pid = getpid();
    request.header.nlmsg_type = NLMSG_DONE;
    request.header.nlmsg_len = sizeof(request);

	if (send(fSocket, &request, sizeof(request), 0) == -1) {
		close(fSocket);
		TRACE("SystemWatcher: Error sending request\n");
		return B_ERROR;
	}

	fBuf = (struct nlmsghdr*)malloc(CONNECTOR_MAX_MSG_SIZE);
	if (fBuf == NULL) {
		close(fSocket);
		TRACE("SystemWatcher: Memory error\n");
		return B_NO_MEMORY;
	}

	fThread = spawn_thread(_WatchTask, "SystemWatcher", 5, NULL);
	if (fThread <= 0) {
		free(fBuf);
		close(fSocket);
		TRACE("SystemWatcher: Can't spawn receiver thread\n");
		return B_NO_MEMORY;
	}

	fRunning = true;

	return resume_thread(fThread);
}


int
SystemWatcher::_WatchTask(void* cookie)
{
	if (fInstance == NULL)
		return -1;

	fInstance->WatchTask();
	return 0;
}


void
SystemWatcher::WatchTask()
{
	CALLED();

	while (1) {
		socklen_t len = sizeof(fAddr);

		ssize_t count = recvfrom(fSocket, fBuf, CONNECTOR_MAX_MSG_SIZE,
			0, (struct sockaddr*)&fAddr, &len);

		if (fAddr.nl_pid == 0) {
			struct nlmsghdr* header = fBuf;
			for (header = fBuf; NLMSG_OK(header, count);
					header = NLMSG_NEXT(header, count)) {

				switch (header->nlmsg_type)
				{
					case NLMSG_ERROR:
					case NLMSG_NOOP:
					case NLMSG_OVERRUN:
						continue;

					default:
						HandleProcEvent(NLMSG_DATA(header));
				}
			}
		}
	}

	close(fSocket);
	free(fBuf);
}


void
SystemWatcher::HandleProcEvent(struct cn_msg* header)
{
	AutoLocker<BLocker> _(&fLock);

	if (header->id.val != CN_VAL_PROC || header->id.idx != CN_IDX_PROC)
		return;

	struct proc_event* event = (struct proc_event*)header->data;
	switch (event->what)
	{

	#if __GNUC__ <= 13
		case proc_event::what::PROC_EVENT_FORK:
	#else
		case PROC_EVENT_FORK:
	#endif
		{
			if (event->event_data.fork.child_pid
					== event->event_data.fork.child_tgid) {
				TRACE("fork %d %d\n",
					event->event_data.fork.parent_pid,
					event->event_data.fork.child_pid);

				Notify(B_WATCH_SYSTEM_TEAM_CREATION, B_TEAM_CREATED,
					event->event_data.fork.child_pid);
			} else {
				TRACE("new thread %d %d\n",
					event->event_data.fork.child_tgid,
					event->event_data.fork.child_pid);

				Notify(B_WATCH_SYSTEM_THREAD_CREATION, B_THREAD_CREATED,
					event->event_data.fork.child_pid);
			}
			break;
		}

	#if __GNUC__ <= 13
		case proc_event::what::PROC_EVENT_EXEC:
	#else
		case PROC_EVENT_EXEC:
	#endif
		{
			Notify(B_WATCH_SYSTEM_TEAM_CREATION
					| B_WATCH_SYSTEM_TEAM_DELETION,
				B_TEAM_EXEC, event->event_data.exec.process_pid);
			break;
		}

	#if __GNUC__ <= 13
		case proc_event::what::PROC_EVENT_EXIT:
	#else
		case PROC_EVENT_EXIT:
	#endif
		{
			if (event->event_data.exit.process_pid
					== event->event_data.exit.process_tgid) {
				TRACE("exit process: tid=%d pid=%d exit_code=%d\n",
					event->event_data.exit.process_pid,
					event->event_data.exit.process_tgid,
					event->event_data.exit.exit_code);

				Notify(B_WATCH_SYSTEM_TEAM_DELETION, B_TEAM_DELETED,
					event->event_data.exit.process_pid);
			} else {
				TRACE("exit thread: tid=%d pid=%d exit_code=%d\n",
					event->event_data.exit.process_pid,
					event->event_data.exit.process_tgid,
					event->event_data.exit.exit_code);				

				Notify(B_WATCH_SYSTEM_THREAD_DELETION, B_THREAD_DELETED,
					event->event_data.exit.process_pid);
			}
			break;
		}

		default:
			break;
	}
}


void
SystemWatcher::Notify(uint32 flags, uint32 what, uint32 thread)
{
	for (int32 i = 0; i < fListeners.CountItems(); i++) {
		if (fListeners.ItemAt(i)->flags & flags) {
			char buffer[128];

			KMessage message;
			message.SetTo(buffer, sizeof(buffer), B_SYSTEM_OBJECT_UPDATE);
			message.AddInt32("opcode", what);

			if (what < B_THREAD_CREATED)
				message.AddInt32("team", thread);
			else
				message.AddInt32("thread", thread);

			message.SetDeliveryInfo(fListeners.ItemAt(i)->token,
				fListeners.ItemAt(i)->port, -1, find_thread(NULL));

			int32 kPortMessageCode = 'pjpp';
			write_port(fListeners.ItemAt(i)->port,
				kPortMessageCode, message.Buffer(), message.ContentSize());
		}
	}
}


status_t
SystemWatcher::AddListener(int32 object, uint32 flags,
	port_id port, int32 token)
{
	if (fInstance == NULL)
		fInstance = new SystemWatcher();

	AutoLocker<BLocker> _(&fInstance->fLock);

	if (!fInstance->IsRunning()) {
		status_t err = fInstance->Run();
		if (err != B_OK)
			return err;
	}

	bool ret = fInstance->fListeners.AddItem(
		new WatchListener{object, flags, port, token});

	return ret ? B_OK : B_ERROR;
}


status_t
SystemWatcher::RemoveListener(int32 object, uint32 flags,
	port_id port, int32 token)
{
	if (fInstance == NULL || !fInstance->IsRunning())
		return B_NO_INIT;

	AutoLocker<BLocker> _(&fInstance->fLock);

	for (int i = 0; i < fInstance->fListeners.CountItems(); i++) {
		WatchListener* elem = fInstance->fListeners.ItemAt(i);
		if (elem->object == object && elem->flags == flags
				&& elem->port == port && elem->token == token) {
			fInstance->fListeners.RemoveItem(elem);
			return B_OK;
		}
	}
	return B_ERROR;
}


}


status_t
__start_watching_system(int32 object, uint32 flags,
	port_id port, int32 token)
{
	CALLED();
	return BKernelPrivate::SystemWatcher::AddListener(object, flags,
		port, token);
}


status_t
__stop_watching_system(int32 object, uint32 flags,
	port_id port, int32 token)
{
	CALLED();
	return BKernelPrivate::SystemWatcher::RemoveListener(object, flags,
		port, token);
}
