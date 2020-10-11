#include <Application.h>
#include <Looper.h>
#include <Message.h>
#include <OS.h>

#include <assert.h>
#include <stdio.h>


int main()
{
	team_info info; 
	printf("Testing team info functions");

	status_t ret = get_team_info(0, &info);
	assert(ret == B_OK);
	printf("Current team: %s - id: %d - uid: %d\n", info.args, info.team, info.uid);

	int32 i = 0;
	while (get_next_team_info(&i, &info) == B_OK) {
		printf("Next team info: args %s - id: %d - uid: %d  - thread_count: %d\n",
			info.args, info.team, info.uid, info.thread_count);
	}
	assert(get_next_team_info(&i, &info) == B_BAD_VALUE);

	return 0;
}
