#include <stdio.h>
#include <stdlib.h>
#include <OS.h>

int main(int argc, char** argv)
{
	sem_id ping;
	sem_id pong;

	if (argc == 1)
	{
		ping = create_sem(1, "ping");
		pong = create_sem(0, "pong");
		printf("Now run \"testsempingpong %ld %ld\" from another shell\n",
			ping, pong);
	}
	else
	{
		ping = atol(argv[1]);
		pong = atol(argv[2]);
	}


	if (ping < 0 || pong < 0)
	{
		printf("At least one of the semaphores could not be created.\n");
		return 1;
	}

	for(int i = 0; i < 12; i++)
	{
		if (argc == 1)
		{
			acquire_sem(ping);
			puts("Ping...");
			release_sem(pong);
		}
		else
		{
			acquire_sem(pong);
			puts("Pong...");
			release_sem(ping);
		}
	}

	/* It's a race, but who cares */
	delete_sem(ping);
	delete_sem(pong);

	return 0;
}
