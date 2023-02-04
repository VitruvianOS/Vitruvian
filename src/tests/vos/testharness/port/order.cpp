#include <stdio.h>
#include <string.h>

#include <OS.h>

port_id test_p1;
port_id test_p2;
char testdata[5];
char testdata2[5];


int main()
{
	int32 dummy;
	ssize_t ret;
	test_p1 = create_port(1, "test port #1");
	printf("Created port %d\n", test_p1);
	close_port(test_p1);
	delete_port(test_p1);
	printf("Port %d closed and deleted\n", test_p1);

	test_p1 = create_port(2, "test port #1");
	printf("Created port %d\n", test_p1);

	strcpy(testdata, "abcd");
	status_t status = write_port(test_p1, 1, &testdata, sizeof(testdata));
	if (status != B_OK)
		printf("port err (failure)\n");

	strcpy(testdata, "bacd");
	status = write_port(test_p1, 1, &testdata, sizeof(testdata));
	if (status != B_OK)
		printf("port err (failure)\n");

	ret = read_port(test_p1, &dummy, testdata2, sizeof(testdata));
	if (ret < 0)
		printf("port err (failure)\n");

	printf("Read size: %d Data: %s\n", ret, testdata2);

	if (strcmp("abcd", testdata2) != 0)
		printf("FAILURE data not equal\n");

	ret = read_port(test_p1, &dummy, testdata2, sizeof(testdata));
	if (ret < 0)
		printf("port err (failure)\n");

	printf("Read size: %d Data: %s\n", ret, testdata2);

	if (strcmp("bacd", testdata2) != 0)
		printf("FAILURE data not equal\n");

	printf("Should wait for a read now\n");

	read_port_etc(test_p1, &dummy, testdata, sizeof(testdata), B_TIMEOUT, 2000);
	if (status != B_OK)
		printf("port err (failure)\n");

	status = write_port_etc(test_p1, 1, &testdata, sizeof(testdata), B_TIMEOUT, 2000);
	if (status != B_OK)
		printf("port err (failure)\n");

	status = write_port_etc(test_p1, 1, &testdata, sizeof(testdata), B_TIMEOUT, 2000);
	if (status != B_OK)
		printf("port err (failure)\n");

	printf("Should wait for a write now\n");

	status = write_port_etc(test_p1, 1, &testdata, sizeof(testdata), B_TIMEOUT, 1000000);
	if (status != B_OK)
		printf("port err (success)\n");

	close_port(test_p1);
	delete_port(test_p1);
	return 0;
}
