#include <stdio.h>

extern "C" void
debugger(const char *message)
{
	printf("debugger called\n");
}
