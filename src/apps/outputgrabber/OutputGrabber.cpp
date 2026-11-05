/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// Minimal output add-on: attaches to an output (physical or virtual),
// receives damage frames over the shared-buffer transport, optionally
// writes them as PPM files. The self test creates a virtual output with
// the built-in test pattern and proves frames flow and change.

#include <OutputAddonClient.h>
#include <OutputAddonProtocol.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


static void
_PrintUsage(const char* name)
{
	printf("usage: %s [--screen N] [--virtual WxH] [--frames N]\n"
		"          [--out DIR] [--selftest]\n", name);
}


static status_t
_WritePPM(const char* path, const uint8* bits, int32 width, int32 height,
	int32 bytesPerRow)
{
	FILE* file = fopen(path, "wb");
	if (file == NULL)
		return B_ERROR;

	fprintf(file, "P6\n%d %d\n255\n", width, height);
	for (int32 y = 0; y < height; y++) {
		const uint8* row = bits + (int64)y * bytesPerRow;
		for (int32 x = 0; x < width; x++) {
			fwrite(row + x * 4, 1, 3, file);
		}
	}

	fclose(file);
	return B_OK;
}


static uint32
_Checksum(const uint8* bits, int32 bytesPerRow, int32 height)
{
	uint32 sum = 0;
	for (int32 y = 0; y < height; y++) {
		const uint8* row = bits + (int64)y * bytesPerRow;
		for (int32 x = 0; x < bytesPerRow; x++)
			sum = sum * 33 + row[x];
	}
	return sum;
}


static int
_Capture(OutputAddonClient& client, int32 frames, const char* outDir)
{
	int32 width = client.Width();
	int32 height = client.Height();
	int32 stride = width * 4;

	uint8* image = (uint8*)malloc((size_t)stride * height);
	if (image == NULL)
		return 1;

	uint32 lastChecksum = 0;
	int32 changed = 0;

	for (int32 i = 0; i < frames; i++) {
		output_addon_frame frame;
		output_addon_rect rects[kOutputAddonMaxRects];
		status_t status = client.WaitForFrame(&frame, rects, 3 * 1000000);
		if (status == B_TIMED_OUT) {
			printf("frame %d: timed out\n", i);
			continue;
		}
		if (status != B_OK) {
			printf("frame %d: wait failed: %s\n", i, strerror(status));
			free(image);
			return 1;
		}

		// A newer frame may already be published; take the next one.
		status = client.CopyFrame(image, stride);
		if (status == B_BAD_VALUE)
			continue;
		if (status != B_OK) {
			printf("frame %d: copy failed: %s\n", i, strerror(status));
			free(image);
			return 1;
		}

		uint32 sum = _Checksum(image, stride, height);
		if (sum != lastChecksum)
			changed++;
		lastChecksum = sum;

		printf("frame %d: generation %u, %d rects, checksum %08x\n", i,
			frame.generation, frame.rect_count, sum);

		if (outDir != NULL) {
			char path[1024];
			snprintf(path, sizeof(path), "%s/frame-%04d.ppm", outDir, i);
			_WritePPM(path, image, width, height, stride);
		}
	}

	free(image);
	printf("captured %d frames, %d checksum changes\n", frames, changed);
	return 0;
}


static int
_SelfTest()
{
	OutputAddonClient client;
	status_t status = client.Connect("framegrabber selftest");
	if (status != B_OK) {
		printf("connect failed: %s\n", strerror(status));
		return 1;
	}

	int32 screen = -1;
	status = client.CreateVirtualOutput(320, 240, "grabber-test", true,
		&screen);
	if (status != B_OK) {
		printf("create virtual output failed: %s\n", strerror(status));
		return 1;
	}

	printf("virtual output created: screen %d\n", screen);

	status = client.AttachOutput(screen);
	if (status != B_OK) {
		printf("attach failed: %s\n", strerror(status));
		client.DeleteVirtualOutput(screen);
		return 1;
	}

	// The test pattern moves a bar; frames must differ over time.
	uint32 sums[4] = {0};
	int32 failures = 0;
	int32 stride = client.Width() * 4;
	uint8* image = (uint8*)malloc((size_t)stride * client.Height());
	for (int32 i = 0; i < 4; i++) {
		output_addon_frame frame;
		output_addon_rect rects[kOutputAddonMaxRects];
		status = client.WaitForFrame(&frame, rects, 5 * 1000000);
		if (status != B_OK) {
			printf("selftest: wait %d failed: %s\n", i, strerror(status));
			failures++;
			continue;
		}

		status = client.CopyFrame(image, stride);
		if (status == B_BAD_VALUE) {
			// A newer frame superseded this one before the copy.
			sums[i] = 0;
			continue;
		}
		if (status != B_OK) {
			printf("selftest: copy %d failed: %s\n", i, strerror(status));
			failures++;
			continue;
		}

		sums[i] = _Checksum(image, stride, client.Height());
		snooze(150000);
	}

	free(image);

	int32 distinct = 0;
	for (int32 i = 0; i < 4; i++) {
		for (int32 j = i + 1; j < 4; j++) {
			if (sums[i] != sums[j] && sums[i] != 0 && sums[j] != 0)
				distinct++;
		}
	}

	client.DetachOutput();
	status = client.DeleteVirtualOutput(screen);
	if (status != B_OK) {
		printf("selftest: delete virtual output failed: %s\n",
			strerror(status));
		failures++;
	}

	if (failures == 0 && distinct > 0) {
		printf("selftest: PASS (%d distinct frame pairs)\n", distinct);
		return 0;
	}

	printf("selftest: FAIL (%d failures, %d distinct pairs)\n", failures,
		distinct);
	return 1;
}


int
main(int argc, char** argv)
{
	int32 screen = 0;
	int32 frames = 30;
	bool useVirtual = false;
	int32 virtualWidth = 640;
	int32 virtualHeight = 480;
	const char* outDir = NULL;
	bool selfTest = false;

	for (int32 i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--screen") == 0 && i + 1 < argc) {
			screen = atoi(argv[++i]);
		} else if (strcmp(argv[i], "--virtual") == 0 && i + 1 < argc) {
			useVirtual = true;
			sscanf(argv[++i], "%dx%d", &virtualWidth, &virtualHeight);
		} else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
			frames = atoi(argv[++i]);
		} else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
			outDir = argv[++i];
		} else if (strcmp(argv[i], "--selftest") == 0) {
			selfTest = true;
		} else {
			_PrintUsage(argv[0]);
			return 2;
		}
	}

	if (selfTest)
		return _SelfTest();

	OutputAddonClient client;
	status_t status = client.Connect("framegrabber");
	if (status != B_OK) {
		printf("connect to app_server add-on channel failed: %s\n",
			strerror(status));
		return 1;
	}

	if (useVirtual) {
		status = client.CreateVirtualOutput(virtualWidth, virtualHeight,
			"framegrabber", true, &screen);
		if (status != B_OK) {
			printf("create virtual output failed: %s\n", strerror(status));
			return 1;
		}
	}

	status = client.AttachOutput(screen);
	if (status != B_OK) {
		printf("attach to screen %d failed: %s\n", screen,
			strerror(status));
		return 1;
	}

	printf("attached to screen %d: %dx%d stride %d\n", screen,
		client.Width(), client.Height(), client.BytesPerRow());

	int result = _Capture(client, frames, outDir);

	client.DetachOutput();
	if (useVirtual)
		client.DeleteVirtualOutput(screen);

	return result;
}
