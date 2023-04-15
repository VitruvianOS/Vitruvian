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
		send_data(father, code, (void *)buf, strlen(buf));
		return;
	}

	char recBuf[512];
	thread_id sender;

	code = receive_data(&sender, (void *)recBuf, sizeof(recBuf));

	printf("exit");
	return 0;
}
