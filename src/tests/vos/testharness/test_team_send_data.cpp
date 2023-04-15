#include <OS.h>
#include <stdio.h>


int main(void)
{
	int32 code = 63;
	const char *buf = "Hello";

	thread_id father = find_thread(NULL);
	team_id team = fork();

	if (team < 0)
		return;

	if (team == 0) {
		status_t ret = send_data(father, code, (void *)buf, strlen(buf));
		if (ret != B_OK)
			printf("FAIL\n");
		return;
	}

	char recBuf[512];
	thread_id sender;

	int32 retCode = receive_data(&sender, (void *)recBuf, sizeof(recBuf));

	if (retCode != 63)
		printf("FAIL\n");
	else
		printf("SUCCESS\n");

	return 0;
}
