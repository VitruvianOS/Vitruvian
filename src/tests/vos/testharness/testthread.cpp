#include <OS.h>


int32
thread_test(void *data)
{
   thread_id sender;
   int32 code;
   char buf[512];

   printf("testthread (%s):thread %d should have data\n",
    	(has_data(find_thread(NULL)) == true) ? "pass" : "FAIL", find_thread(NULL));  

   code = receive_data(&sender, (void *)buf, sizeof(buf));

   printf("testthread (%s):thread %d should not have data\n",
    	(has_data(sender) != true) ? "pass" : "FAIL", sender);  

   if (code != 63)
	printf("FAIL WRONG CODE!!!\n");

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

   printf("testthread (%s):thread %d should not have data\n",
		(has_data(other_thread) == false) ? "pass" : "FAIL", other_thread);  

   send_data(other_thread, code, (void *)buf, strlen(buf));

   printf("testthread (%s):thread %d should have data\n",
		(has_data(other_thread) == true) ? "pass" : "FAIL", other_thread);  

   resume_thread(other_thread);
}
