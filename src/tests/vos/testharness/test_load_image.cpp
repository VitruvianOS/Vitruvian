#include <image.h>
#include <OS.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int main() {
    char **arg_v;
    int arg_c;
    team_id exec_thread;
    status_t return_value;

    arg_c = 3; 
    arg_v = (char **)malloc(sizeof(char *) * (arg_c + 1));

    arg_v[0] = strdup("/bin/echo");
    arg_v[1] = strdup("Hello,");
    arg_v[2] = strdup("world!");
    arg_v[3] = NULL;

    exec_thread = load_image(arg_c, arg_v, environ);
    if (exec_thread < 0)
		printf("FAIL");

    while (--arg_c >= 0)
        free(arg_v[arg_c]);
    free(arg_v);

    status_t ret = wait_for_thread(exec_thread, &return_value);
	if (ret != B_OK)
		printf("FAIL");

    printf("The echo operation completed with return value: %d\n", return_value);

	if (return_value != B_OK)
		printf("FAIL");

    return 0;
}
