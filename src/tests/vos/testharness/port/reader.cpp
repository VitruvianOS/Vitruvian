#include <stdio.h>
#include <string.h>

#include <OS.h>


#include <sys/inotify.h>
#include <limits.h>

port_id test_p1;
port_id test_p2;
char testdata[5];


int main()
{
	int32 dummy;
	ssize_t ret;
	test_p1 = create_port(1, "test port #1");
	ret = read_port(test_p1, &dummy, testdata, sizeof(testdata));
	if (ret < 0)
		printf("port err\n");
	printf("Read size: %d Data: %s\n", ret, testdata);

	memset(testdata, 0, sizeof(testdata));
	test_p2 = find_port("test_port_#2");
	ret = read_port(test_p2, &dummy, testdata, sizeof(testdata));
	if (ret < 0)
		printf("port err\n");
	printf("Read size: %d Data: %s\n", ret, testdata);

	//memset(testdata, 0, sizeof(testdata));
	//ret = read_port(test_p2, &dummy, testdata, sizeof(testdata));
	//printf("exited successfully\n");

	close_port(test_p1);
	delete_port(test_p1);
	return 0;
}
