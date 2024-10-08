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
	static void		InitTeam();
	static void		DeInitTeam();

	static void 	PrepareFatherAtFork();
	static void 	SyncFatherAtFork();
	static void 	ReinitChildAtFork();

	static int		GetNexusDescriptor();

private:
	static sem_id	fForkSem;
};


}


#endif
