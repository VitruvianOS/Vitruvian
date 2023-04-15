/*
 *  Copyright 2018-2020, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#ifndef _LIBROOT2_TEAM
#define _LIBROOT2_TEAM

namespace BKernelPrivate {


extern "C" void init_ports();


class Team
{
public:
	static void	InitTeam();
	static void	DeInitTeam();
	static void ReinitAtFork();

	static int	GetNexusDescriptor();
};


}


#endif
