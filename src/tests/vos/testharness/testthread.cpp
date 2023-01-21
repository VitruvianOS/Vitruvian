#include <OS.h>


int32
thread_test(void *data)
{
   thread_id sender;
   int32 code;
   char buf[512];

   code = receive_data(&sender, (void *)buf, sizeof(buf));
   if (code != 63)
	printf("ERROR: WRONG CODE!!!\n");

   printf("Received: \"%s\" from thread %d\n", buf, find_thread(NULL));

   return 0;
}


int
main()
{
   thread_id other_thread;
   int32 code = 63;
   const char *buf = "Hello";

   other_thread = spawn_thread(thread_test, "test", 5, NULL);
   send_data(other_thread, code, (void *)buf, strlen(buf));
   resume_thread(other_thread);

	sleep(3);
}
