#include <OS.h>

static void port_test();


int32
thread_test(void *data)
{
   thread_id sender;
   int32 code;
   char buf[512];

   code = receive_data(&sender, (void *)buf, sizeof(buf));
   if (code != B_OK)
	printf("err\n");

   printf("Received: %s %d\n", buf, find_thread(NULL));
}


int
main()
{
   thread_id other_thread;
   int32 code = 63;
   char *buf = "Hello";

   other_thread = spawn_thread(thread_test, "test", 5, NULL);
   send_data(other_thread, code, (void *)buf, strlen(buf));
   printf("%d\n", other_thread);
   resume_thread(other_thread);

	sleep(3);
}
