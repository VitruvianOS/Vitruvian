/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// Prints the signature an X/Qt client gets: xclientsig <executable>

#include <dlfcn.h>
#include <stdio.h>


int
main(int argc, char** argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: xclientsig <executable>\n");
		return 2;
	}
	void* lib = dlopen("libvos_x_client.so", RTLD_NOW);
	if (lib == NULL) {
		fprintf(stderr, "xclientsig: %s\n", dlerror());
		return 1;
	}
	typedef int (*signature_fn)(const char*, char*, size_t);
	signature_fn fn = (signature_fn)dlsym(lib, "vos_x_client_signature");
	char out[256];
	if (fn == NULL || fn(argv[1], out, sizeof(out)) != 0)
		return 1;
	puts(out);
	return 0;
}
