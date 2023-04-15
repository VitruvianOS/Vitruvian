#include <OS.h>
#include <stdio.h>


int main(void)
{
	port_id port = create_port(1, "semper ad maiora");
	if (port < 0) {
		printf("create_port FAIL\n");
		return 1;
	}

	team_id team = fork();

	if (team < 0) {
		printf("fork FAIL\n");
		return 1;
	}

	if (team == 0) {
		return 1;
	}

	if (set_port_owner(port, team) != B_OK) {
		printf("set_port_owner FAIL\n");
		return 1;
	}

	port_info info;

	if (get_port_info(port, &info) != B_OK || info.team != team) {
		printf("fork FAIL\n");
		return 1;
	}

	return 0;
}
