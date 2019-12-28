/*
 *  Copyright 2019, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include <Locker.h>
#include <ObjectList.h>
#include <OS.h>

#include <linux/netlink.h>
#include <sys/socket.h>


struct WatchListener {
	int32 object;
	uint32 flags;
	port_id port;
	int32 token;
};


class SystemWatcher {
public:
	static status_t				AddListener(int32 object, uint32 flags,
									port_id port, int32 token);

	static status_t				RemoveListener(int32 object, uint32 flags,
									port_id port, int32 token);

private:
								SystemWatcher();

	status_t					Run();
	status_t					Stop();

	bool						IsRunning() const;

	static int					_WatchTask(void* cookie);
	void						WatchTask();
	void						HandleProcEvent(struct cn_msg* header);

	void						Notify(uint32 flags, uint32 what, uint32 team);

	BObjectList<WatchListener>	fListeners;
	bool						fRunning;

	thread_id					fThread;

	int							fSocket;
	struct sockaddr_nl			fAddr;
	struct nlmsghdr*			fBuf;

	BLocker						fLock;
	static SystemWatcher* 		fInstance;
};
