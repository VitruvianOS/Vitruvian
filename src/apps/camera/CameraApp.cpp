/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#include "CameraApp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Node.h>
#include <String.h>

#include "CameraWindow.h"


static void
_WriteWavHeader(BFile& file, uint32 sampleRate, uint32 dataBytes)
{
	uint8 header[44];
	memset(header, 0, sizeof(header));
	memcpy(header, "RIFF", 4);
	const uint32 riffSize = 36 + dataBytes;
	header[4] = (uint8)(riffSize);
	header[5] = (uint8)(riffSize >> 8);
	header[6] = (uint8)(riffSize >> 16);
	header[7] = (uint8)(riffSize >> 24);
	memcpy(header + 8, "WAVEfmt ", 8);
	header[16] = 16;
	header[20] = 1;		// PCM
	header[22] = 1;		// mono
	header[24] = (uint8)(sampleRate);
	header[25] = (uint8)(sampleRate >> 8);
	header[26] = (uint8)(sampleRate >> 16);
	header[27] = (uint8)(sampleRate >> 24);
	const uint32 byteRate = sampleRate * 2;
	header[28] = (uint8)(byteRate);
	header[29] = (uint8)(byteRate >> 8);
	header[30] = (uint8)(byteRate >> 16);
	header[31] = (uint8)(byteRate >> 24);
	header[32] = 2;		// block align
	header[34] = 16;	// bits per sample
	memcpy(header + 36, "data", 4);
	header[40] = (uint8)(dataBytes);
	header[41] = (uint8)(dataBytes >> 8);
	header[42] = (uint8)(dataBytes >> 16);
	header[43] = (uint8)(dataBytes >> 24);
	file.Write(header, sizeof(header));
}


CameraApp::CameraApp()
	:
	BApplication("application/x-vnd.VOS-CameraApp")
{
	fSettings.MakeEmpty();

	BPath configPath;
	if (find_directory(B_USER_CONFIG_DIRECTORY, &configPath, true) == B_OK) {
		BString settingsPath(configPath.Path());
		settingsPath << "/Camera";
		fSettingsPath.SetTo(settingsPath.String());

		BFile file(fSettingsPath.Path(), B_READ_ONLY);
		if (file.InitCheck() == B_OK) {
			if (fSettings.Unflatten(&file) != B_OK)
				fSettings.MakeEmpty();
		}
	}
}


status_t
CameraApp::SaveSettings()
{
	if (fSettingsPath.Path() == NULL)
		return B_ERROR;

	BFile file(fSettingsPath.Path(),
		B_CREATE_FILE | B_WRITE_ONLY | O_TRUNC);
	if (file.InitCheck() != B_OK)
		return file.InitCheck();

	return fSettings.Flatten(&file);
}


status_t
CameraApp::ShutterSoundRef(entry_ref* outRef)
{
	if (outRef == NULL)
		return B_BAD_VALUE;

	BPath configPath;
	if (find_directory(B_USER_CONFIG_DIRECTORY, &configPath, true) != B_OK)
		return B_ERROR;

	BString soundPath(configPath.Path());
	soundPath << "/Camera";
	BDirectory dir;
	BEntry entry(soundPath.String());
	if (!entry.Exists()) {
		// one level is enough here: config dir exists
		BDirectory parent(configPath.Path());
		parent.CreateDirectory("Camera", &dir);
	} else {
		dir.SetTo(soundPath.String());
	}
	soundPath << "/shutter.wav";

	BEntry existing(soundPath.String());
	if (existing.Exists())
		return existing.GetRef(outRef);

	BFile file(soundPath.String(), B_CREATE_FILE | B_WRITE_ONLY | O_TRUNC);
	if (file.InitCheck() != B_OK)
		return file.InitCheck();

	const uint32 rate = 22050;
	const uint32 frames = rate * 35 / 1000;
	const uint32 dataBytes = frames * 2;
	_WriteWavHeader(file, rate, dataBytes);

	for (uint32 i = 0; i < frames; i++) {
		const float t = (float)i / (float)rate;
		const float envelope = expf(-t * 90.0f);
		const float sample = sinf(2.0f * 3.14159265f * 2400.0f * t)
			* envelope * 0.6f;
		const int16 value = (int16)(sample * 32000.0f);
		file.Write(&value, sizeof(value));
	}

	BEntry created(soundPath.String());
	return created.GetRef(outRef);
}


void
CameraApp::ReadyToRun()
{
	CameraWindow* window = new CameraWindow(BRect(80, 80, 80 + 960,
		80 + 640));
	window->Show();
}


void
CameraApp::RefsReceived(BMessage* message)
{
	// Incoming images/videos just activate the window; capture is the job.
}


int
main()
{
	CameraApp app;
	app.Run();
	return 0;
}
