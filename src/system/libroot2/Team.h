/*
 *  Copyright 2018-2026, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#ifndef _LIBROOT2_TEAM
#define _LIBROOT2_TEAM

#include <OS.h>
#include <libudev.h>


namespace BKernelPrivate {


class Team
{
public:
	static void			InitTeam();

	static int			GetNexusDescriptor();
	static int			GetSemDescriptor();
	static int			GetAreaDescriptor();
	static int			GetVRefDescriptor(dev_t* dev = NULL);
	static int			GetNodeMonitorDescriptor();
	static struct udev*	GetUDev();

	static mode_t		GetUmask();
	static int32		GetCPUCount();

	static thread_id	LoadImage(int32 argc, const char** argv,
							const char** envp);

	static void 		PrepareFatherAtFork();
	static void 		SyncFatherAtFork();
	static void 		ReinitChildAtFork();
};


}


#endif
