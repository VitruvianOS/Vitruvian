#include <stdio.h>
#include <string.h>

#include <OS.h>


port_id test_p1;
port_id test_p2;
char testdata[5];


int main()
{
	test_p2 = create_port(1, "test_port_#2");
	test_p1 = find_port("test port #1");
	strcpy(testdata, "abcd");
	status_t status = write_port(test_p1, 1, &testdata, sizeof(testdata));
	if (status != B_OK)
		printf("port err\n");

	strcpy(testdata, "abcd2");
	status = write_port(test_p2, 1, &testdata, sizeof(testdata));
	if (status != B_OK)
		printf("port err\n");

	sleep(6);

	//close_port(test_p2);
	//delete_port(test_p2);
	return 0;
}
