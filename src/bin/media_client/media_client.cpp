/*
 * Copyright 2016-2017, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <Application.h>
#include <String.h>

#include <stdio.h>
#include <string.h>

#include <media2/MediaGraph.h>

#include "MediaPlay.h"
#include "MediaTest.h"
#include "MidiPlay.h"


void print_usage()
{
	printf("Usage:\n");
	printf("  media_client play <uri>\n");
	printf("  media_client playmidi <uri>\n");
	printf("  media_client test\n");
	printf("  media_client dump\n");
}


static int
media_dump()
{
	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL) {
		printf("media graph unavailable\n");
		return 1;
	}

	BString out;
	status_t status = graph->DumpGraph(out);
	if (status != B_OK) {
		printf("dump failed: %s\n", strerror(status));
		return 1;
	}
	printf("%s", out.String());
	return 0;
}


int main(int argc, char *argv[])
{
	if (argc < 2) {
		print_usage();
		return 0;
	}

	BApplication app("application/x-vnd.Haiku-media_client");

	int ret = 0;
	if (strcmp(argv[1], "play") == 0) {
		if (argc < 3) {
			print_usage();
		} else
			ret = media_play(argv[2]);
	} else if (strcmp(argv[1], "playmidi") == 0) {
		if (argc < 3) {
			print_usage();
		} else
			ret = media_midi_play(argv[2]);
	} else if (strcmp(argv[1], "test") == 0)
		media_test();
	else if (strcmp(argv[1], "dump") == 0)
		ret = media_dump();
	else
		print_usage();

	return ret;
}
