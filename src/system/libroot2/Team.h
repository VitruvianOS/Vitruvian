/*
 *  Copyright 2018-2020, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#ifndef _LIBROOT2_TEAM
#define _LIBROOT2_TEAM

#include <OS.h>


namespace BKernelPrivate {


class Team
{
public:
	static void			InitTeam();

	static status_t		WaitForTeam(team_id id, status_t* _returnCode);
	static thread_id	LoadImage(int32 argc, const char** argv,
							const char** envp);

	static void 		PrepareFatherAtFork();
	static void 		SyncFatherAtFork();
	static void 		ReinitChildAtFork();

	static int			GetNexusDescriptor();
	static int			GetSemDescriptor();
	static int			GetAreaDescriptor();
	static int			GetVRefDescriptor(dev_t* dev = NULL);

private:
	static sem_id		fForkSem;
};


}


#endif
