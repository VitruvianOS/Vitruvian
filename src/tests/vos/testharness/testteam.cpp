// Standard Includes -----------------------------------------------------------
#include <stdio.h>

// System Includes -------------------------------------------------------------
#include <Application.h>
#include <Looper.h>
#include <Message.h>
#include <OS.h>
#include "syscalls.h"

// Project Includes ------------------------------------------------------------

// Local Includes --------------------------------------------------------------

// Local Defines ---------------------------------------------------------------

// Globals ---------------------------------------------------------------------

int main()
{

	team_info info; 
    printf("Next team_info");
    int32 counter = 0;
    int ret_code = _kern_get_team_info(0, &info);
    printf("Ret code for id == 0: %d\n", ret_code);
    printf("info: %s - id: %d - uid: %d\n", info.args, info.team, info.uid);
    _kern_get_next_team_info(&counter, &info);
    printf("cookie: %d - info: %s - id: %d - uid: %d\n", counter, info.args, info.team, info.uid);
    _kern_get_next_team_info(&counter, &info);
    printf("cookie: - %d info: %s - id: %d - uid: %d\n", counter, info.args, info.team, info.uid);


	return 0;
}
