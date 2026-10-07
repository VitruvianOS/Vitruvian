/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <media2/SoundFontSynth.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <String.h>

// fluidsynth 2.x headers. When the -dev package is absent the build check
// is MISSING; the code stays written against the documented 2.x API.
#if defined(__has_include)
#	if __has_include(<fluidsynth.h>)
#		include <fluidsynth.h>
#		define VOS_HAVE_FLUIDSYNTH 1
#	endif
#endif
#ifndef VOS_HAVE_FLUIDSYNTH
// Minimal stand-ins so the rest of the kit still syntax-checks without the
// -dev package. Real builds define VOS_HAVE_FLUIDSYNTH via the header.
typedef struct _fluid_settings_t fluid_settings_t;
typedef struct _fluid_synth_t fluid_synth_t;
#define FLUID_FAILED (-1)
extern "C" {
fluid_settings_t* new_fluid_settings(void);
void delete_fluid_settings(fluid_settings_t*);
fluid_synth_t* new_fluid_synth(fluid_settings_t*);
void delete_fluid_synth(fluid_synth_t*);
int fluid_settings_setnum(fluid_settings_t*, const char*, double);
int fluid_settings_setint(fluid_settings_t*, const char*, int);
int fluid_synth_sfload(fluid_synth_t*, const char*, int);
int fluid_synth_write_float(fluid_synth_t*, int, void*, int, int, void*, int, int);
int fluid_synth_noteon(fluid_synth_t*, int, int, int);
int fluid_synth_noteoff(fluid_synth_t*, int, int);
int fluid_synth_key_pressure(fluid_synth_t*, int, int, int);
int fluid_synth_cc(fluid_synth_t*, int, int, int);
int fluid_synth_program_change(fluid_synth_t*, int, int);
int fluid_synth_channel_pressure(fluid_synth_t*, int, int);
int fluid_synth_pitch_bend(fluid_synth_t*, int, int);
int fluid_synth_sysex(fluid_synth_t*, const char*, int, void*, void*, void*, int);
int fluid_synth_system_reset(fluid_synth_t*);
void fluid_synth_set_gain(fluid_synth_t*, double);
}
#endif


struct BSoundFontSynth::Fluid {
	fluid_settings_t*	settings;
	fluid_synth_t*		synth;
	int					soundFontId;
};


static const char* kSoundFontCandidates[] = {
	// Prefer the smallest packaged SF3 GM bank; SF2 is the fallback.
	"/usr/share/sounds/sf3/default-GM.sf3",
	"/usr/share/sounds/sf3/default.sf3",
	"/usr/share/sounds/sf3/",
	"/usr/share/sounds/sf2/default-GM.sf2",
	"/usr/share/sounds/sf2/TimGM6mb.sf2",
	"/usr/share/soundfonts/default.sf2",
	"/usr/share/soundfonts/TimGM6mb.sf2",
	"/usr/share/sounds/sf2/misc/TimGM6mb.sf2",
	NULL
};


BSoundFontSynth::BSoundFontSynth(const char* name, uint32 channelCount)
	:
	BMediaSynth(name, channelCount),
	fFluid(NULL),
	fInitStatus(B_NO_INIT),
	fGain(0.2f),
	fInterpolation(1),
	fReverb(true),
	fPolyphony(256),
	fVolume(1.0f)
{
	const char* path = _FindDefaultSoundFont();
	if (path != NULL)
		fInstrumentFile.SetTo(path);

	// Engine build is app-thread work; Start() calls SynthReset() which
	// builds fluidsynth and loads the bank.
}


BSoundFontSynth::~BSoundFontSynth()
{
	SynthShutdown();
}


status_t
BSoundFontSynth::InitCheck() const
{
	return fInitStatus;
}


const char*
BSoundFontSynth::_FindDefaultSoundFont()
{
	// A settings file path is the lookup mechanism upstream uses; reproduce
	// it with a built-in candidate list, SF3 first then SF2.
	for (int32 i = 0; kSoundFontCandidates[i] != NULL; i++) {
		const char* path = kSoundFontCandidates[i];
		BEntry entry(path);
		if (entry.Exists()) {
			if (entry.IsDirectory()) {
				BDirectory dir(path);
				BEntry child;
				if (dir.GetNextEntry(&child) == B_OK) {
					BPath childPath;
					if (child.GetPath(&childPath) == B_OK)
						return strdup(childPath.Path());
				}
			} else {
				return strdup(path);
			}
		}
	}

	// B_SYSTEM_DATA_DIRECTORY/synth/*.sf2, matching upstream's settings path.
	BPath dataPath;
	if (find_directory(B_SYSTEM_DATA_DIRECTORY, &dataPath, true) == B_OK) {
		BString synthDir(dataPath.Path());
		synthDir << "/synth";
		BDirectory dir(synthDir.String());
		if (dir.InitCheck() == B_OK) {
			BEntry child;
			while (dir.GetNextEntry(&child) == B_OK) {
				BPath childPath;
				if (child.GetPath(&childPath) != B_OK)
					continue;
				const char* name = childPath.Path();
				size_t len = strlen(name);
				if (len > 4 && (strcmp(name + len - 4, ".sf2") == 0
					|| strcmp(name + len - 4, ".sf3") == 0))
					return strdup(name);
			}
		}
	}

	return NULL;
}


void
BSoundFontSynth::_ApplySettingsToSynth()
{
	if (fFluid == NULL || fFluid->settings == NULL)
		return;
	fluid_settings_setnum(fFluid->settings, "synth.gain", fGain);
	fluid_settings_setnum(fFluid->settings, "synth.polyphony",
		(double)fPolyphony);
	fluid_settings_setint(fFluid->settings, "synth.reverb.active",
		fReverb ? 1 : 0);
	// FLUID_INTERP_LINEAR is 1; map B_* modes onto fluidsynth's integers.
	fluid_settings_setint(fFluid->settings, "synth.interpolation",
		fInterpolation);
}


status_t
BSoundFontSynth::SynthReset(uint32 sampleRate)
{
	if (sampleRate == 0)
		sampleRate = 48000;

	SynthShutdown();

	fFluid = new(std::nothrow) Fluid();
	if (fFluid == NULL)
		return B_NO_MEMORY;
	fFluid->settings = NULL;
	fFluid->synth = NULL;
	fFluid->soundFontId = -1;

	fFluid->settings = new_fluid_settings();
	if (fFluid->settings == NULL) {
		delete fFluid;
		fFluid = NULL;
		return B_NO_MEMORY;
	}

	fluid_settings_setnum(fFluid->settings, "synth.sample-rate",
		(double)sampleRate);
	_ApplySettingsToSynth();

	fFluid->synth = new_fluid_synth(fFluid->settings);
	if (fFluid->synth == NULL) {
		delete_fluid_settings(fFluid->settings);
		delete fFluid;
		fFluid = NULL;
		return B_ERROR;
	}

	if (fInstrumentFile.Length() == 0) {
		const char* path = _FindDefaultSoundFont();
		if (path != NULL) {
			fInstrumentFile.SetTo(path);
			free((void*)path);
		}
	}

	if (fInstrumentFile.Length() == 0) {
		// No soundfont: fail InitCheck. Never run a synth that renders
		// silence and reports success.
		SynthShutdown();
		fInitStatus = B_ERROR;
		return B_ERROR;
	}

	fFluid->soundFontId = fluid_synth_sfload(fFluid->synth,
		fInstrumentFile.String(), 1);
	if (fFluid->soundFontId == FLUID_FAILED) {
		SynthShutdown();
		fInitStatus = B_ERROR;
		return B_ERROR;
	}

	fInitStatus = B_OK;
	return BMediaSynth::SynthReset(sampleRate);
}


void
BSoundFontSynth::SynthShutdown()
{
	if (fFluid != NULL) {
		if (fFluid->synth != NULL) {
			// All notes off and flush voices before teardown.
			fluid_synth_system_reset(fFluid->synth);
			delete_fluid_synth(fFluid->synth);
		}
		if (fFluid->settings != NULL)
			delete_fluid_settings(fFluid->settings);
		delete fFluid;
		fFluid = NULL;
	}
	fInitStatus = B_NO_INIT;
}


status_t
BSoundFontSynth::LoadInstrumentFile(const char* path)
{
	if (path == NULL)
		return B_BAD_VALUE;
	if (IsStarted())
		return B_NOT_ALLOWED;

	fInstrumentFile.SetTo(path);
	if (fFluid != NULL && fFluid->synth != NULL) {
		int id = fluid_synth_sfload(fFluid->synth, path, 1);
		if (id == FLUID_FAILED)
			return B_ERROR;
		fFluid->soundFontId = id;
	}
	return B_OK;
}


const char*
BSoundFontSynth::InstrumentFile() const
{
	return fInstrumentFile.String();
}


void
BSoundFontSynth::SetGain(float gain)
{
	fGain = gain;
	if (fFluid != NULL && fFluid->synth != NULL)
		fluid_synth_set_gain(fFluid->synth, gain);
}


float
BSoundFontSynth::Gain() const
{
	return fGain;
}


void
BSoundFontSynth::SetInterpolation(int32 mode)
{
	fInterpolation = mode;
	if (fFluid != NULL && fFluid->settings != NULL)
		fluid_settings_setint(fFluid->settings, "synth.interpolation", mode);
}


int32
BSoundFontSynth::Interpolation() const
{
	return fInterpolation;
}


void
BSoundFontSynth::SetReverb(bool enabled)
{
	fReverb = enabled;
	if (fFluid != NULL && fFluid->settings != NULL)
		fluid_settings_setint(fFluid->settings, "synth.reverb.active",
			enabled ? 1 : 0);
}


bool
BSoundFontSynth::IsReverbEnabled() const
{
	return fReverb;
}


void
BSoundFontSynth::SetPolyphony(int32 voices)
{
	if (voices < 1)
		return;
	fPolyphony = voices;
	if (fFluid != NULL && fFluid->synth != NULL)
		fluid_settings_setint(fFluid->settings, "synth.polyphony", voices);
}


int32
BSoundFontSynth::Polyphony() const
{
	return fPolyphony;
}


void
BSoundFontSynth::SetVolume(float volume)
{
	fVolume = volume;
	// fluidsynth gain is the volume surface; keep both in sync.
	SetGain(fGain * volume);
}


float
BSoundFontSynth::Volume() const
{
	return fVolume;
}


void
BSoundFontSynth::Render(float** outputs, uint32 channelCount,
	uint32 frameCount)
{
	if (fFluid == NULL || fFluid->synth == NULL || outputs == NULL
		|| frameCount == 0)
		return;

	void* left = outputs[0];
	void* right = channelCount > 1 ? outputs[1] : outputs[0];
	// Mono DSP ports mean stride 1 and no interleave.
	fluid_synth_write_float(fFluid->synth, (int)frameCount,
		left, 0, 1, right, 0, 1);
}


void
BSoundFontSynth::NoteOn(uchar channel, uchar note, uchar velocity,
	uint32)
{
	if (fFluid == NULL || fFluid->synth == NULL)
		return;
	fluid_synth_noteon(fFluid->synth, channel, note, velocity);
}


void
BSoundFontSynth::NoteOff(uchar channel, uchar note, uchar velocity,
	uint32)
{
	if (fFluid == NULL || fFluid->synth == NULL)
		return;
	fluid_synth_noteoff(fFluid->synth, channel, note);
}


void
BSoundFontSynth::KeyPressure(uchar channel, uchar note, uchar pressure,
	uint32)
{
	if (fFluid == NULL || fFluid->synth == NULL)
		return;
	fluid_synth_key_pressure(fFluid->synth, channel, note, pressure);
}


void
BSoundFontSynth::ControlChange(uchar channel, uchar controlNumber,
	uchar controlValue, uint32)
{
	if (fFluid == NULL || fFluid->synth == NULL)
		return;
	fluid_synth_cc(fFluid->synth, channel, controlNumber, controlValue);
}


void
BSoundFontSynth::ProgramChange(uchar channel, uchar programNumber,
	uint32)
{
	if (fFluid == NULL || fFluid->synth == NULL)
		return;
	fluid_synth_program_change(fFluid->synth, channel, programNumber);
}


void
BSoundFontSynth::ChannelPressure(uchar channel, uchar pressure,
	uint32)
{
	if (fFluid == NULL || fFluid->synth == NULL)
		return;
	fluid_synth_channel_pressure(fFluid->synth, channel, pressure);
}


void
BSoundFontSynth::PitchBend(uchar channel, uchar lsb, uchar msb,
	uint32)
{
	if (fFluid == NULL || fFluid->synth == NULL)
		return;
	int value = ((int)msb << 7) | (lsb & 0x7f);
	fluid_synth_pitch_bend(fFluid->synth, channel, value);
}


void
BSoundFontSynth::SystemExclusive(void* data, size_t length, uint32)
{
	if (fFluid == NULL || fFluid->synth == NULL || data == NULL)
		return;
	fluid_synth_sysex(fFluid->synth, (const char*)data, (int)length,
		NULL, NULL, NULL, 0);
}
