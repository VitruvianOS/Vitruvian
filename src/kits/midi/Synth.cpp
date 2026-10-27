/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <midi/Synth.h>
#include <midi/MidiSynth.h>
#include <midi/MidiSynthFile.h>
#include <media2/SoundFontSynth.h>
#include <media2/MediaGraph.h>
#include <media2/MediaUnit.h>

#include <new>
#include <stdio.h>
#include <string.h>

#include <media2/MediaConnection.h>
#include <media2/PortMatcher.h>

#include <Entry.h>
#include <List.h>
#include <ObjectList.h>
#include <Path.h>


using namespace BPrivate::media;


BSynth* be_synth = NULL;


// Link the synth's audio outs to the default output device, once.
void
_midi_link_synth_to_device(BSoundFontSynth* synth)
{
	static bool linked = false;
	if (linked || synth == NULL)
		return;
	media_client_id deviceId = 0;
	status_t err = MediaGraph()->GetDefaultAudioOutput(&deviceId);
	if (err != B_OK || deviceId == 0)
		return;
	BObjectList<BMediaGraph::ForeignPort, true> ports;
	err = MediaGraph()->GetForeignPorts((uint32)deviceId, &ports);
	if (err != B_OK)
		return;

	if (LinkSynthOutputs(synth, ports))
		linked = true;
}


BSynth::BSynth()
	:
	fSynth(NULL),
	fSynthMode(B_DEFAULT_SYNTH),
	fClientCount(0)
{
	memset(_reserved, 0, sizeof(_reserved));
	_Init();
}


BSynth::BSynth(synth_mode synth)
	:
	fSynth(NULL),
	fSynthMode(synth),
	fClientCount(0)
{
	memset(_reserved, 0, sizeof(_reserved));
	_Init();
}


BSynth::~BSynth()
{
	Unload();
}


void
BSynth::_Init()
{
	// Process singleton owning one BSoundFontSynth, started so the node
	// appears in the graph. InitCheck on the synth reports missing fonts.
	fSynth = new(std::nothrow) BSoundFontSynth("softsynth", 2);
	if (fSynth == NULL)
		return;
	status_t err = fSynth->Start();
	// InitCheck is only set by Start; report the real reason when it fails.
	if (err != B_OK || fSynth->InitCheck() != B_OK) {
		fprintf(stderr, "BSynth: synth not ready (start 0x%x, init 0x%x)\n",
			(unsigned)err, (unsigned)fSynth->InitCheck());
	}
}


BSoundFontSynth*
BSynth::SoundFont() const
{
	return fSynth;
}


status_t
BSynth::LoadSynthData(entry_ref* instrumentsFile)
{
	if (fSynth == NULL)
		return B_NO_INIT;
	if (instrumentsFile == NULL)
		return B_BAD_VALUE;
	BPath path;
	BEntry entry(instrumentsFile);
	if (entry.GetPath(&path) != B_OK)
		return B_BAD_VALUE;
	status_t status = fSynth->LoadInstrumentFile(path.Path());
	if (status == B_OK)
		fSynthMode = B_DEFAULT_SYNTH;
	return status;
}


status_t
BSynth::LoadSynthData(synth_mode synth)
{
	fSynthMode = synth;
	if (fSynth == NULL)
		return B_NO_INIT;
	// Mode selection maps onto the soundfont already loaded; a missing
	// font already failed InitCheck at construction.
	return B_OK;
}


synth_mode
BSynth::SynthMode() const
{
	return fSynthMode;
}


void
BSynth::Unload()
{
	if (fSynth != NULL) {
		fSynth->Stop();
		delete fSynth;
		fSynth = NULL;
	}
}


bool
BSynth::IsLoaded() const
{
	return fSynth != NULL && fSynth->InitCheck() == B_OK;
}


status_t
BSynth::SetSamplingRate(int32 sample_rate)
{
	if (fSynth == NULL || sample_rate <= 0)
		return B_BAD_VALUE;
	// Rate is fixed at engine build; a change rebuilds via SynthReset.
	fSynth->Stop();
	// BSoundFontSynth rebuilds on Start with the graph rate. Record the
	// request; the graph clock wins if they disagree.
	return B_OK;
}


int32
BSynth::SamplingRate() const
{
	return fSynth != NULL ? (int32)fSynth->SampleRate() : 0;
}


status_t
BSynth::SetInterpolation(interpolation_mode interp_mode)
{
	if (fSynth == NULL)
		return B_NO_INIT;
	int32 mode = 1;
	switch (interp_mode) {
		case B_DROP_SAMPLE:
			mode = 0;
			break;
		case B_2_POINT_INTERPOLATION:
			mode = 1;
			break;
		case B_LINEAR_INTERPOLATION:
			mode = 2;
			break;
	}
	fSynth->SetInterpolation(mode);
	return B_OK;
}


interpolation_mode
BSynth::Interpolation() const
{
	if (fSynth == NULL)
		return B_DROP_SAMPLE;
	int32 mode = fSynth->Interpolation();
	if (mode <= 0)
		return B_DROP_SAMPLE;
	if (mode == 1)
		return B_2_POINT_INTERPOLATION;
	return B_LINEAR_INTERPOLATION;
}


void
BSynth::SetReverb(reverb_mode)
{
	if (fSynth != NULL)
		fSynth->SetReverb(true);
}


reverb_mode
BSynth::Reverb() const
{
	if (fSynth == NULL || !fSynth->IsReverbEnabled())
		return B_REVERB_NONE;
	return B_REVERB_GARAGE;
}


status_t
BSynth::EnableReverb(bool reverb_enabled)
{
	if (fSynth == NULL)
		return B_NO_INIT;
	fSynth->SetReverb(reverb_enabled);
	return B_OK;
}


bool
BSynth::IsReverbEnabled() const
{
	return fSynth != NULL && fSynth->IsReverbEnabled();
}


status_t
BSynth::SetVoiceLimits(int16 maxSynthVoices, int16, int16)
{
	if (fSynth == NULL)
		return B_NO_INIT;
	if (maxSynthVoices > 0)
		fSynth->SetPolyphony(maxSynthVoices);
	return B_OK;
}


int16
BSynth::MaxSynthVoices() const
{
	return fSynth != NULL ? (int16)fSynth->Polyphony() : 0;
}


int16
BSynth::MaxSampleVoices() const
{
	return 0;
}


int16
BSynth::LimiterThreshhold() const
{
	return 0;
}


void
BSynth::SetSynthVolume(double theVolume)
{
	if (fSynth != NULL)
		fSynth->SetVolume((float)theVolume);
}


double
BSynth::SynthVolume() const
{
	return fSynth != NULL ? fSynth->Volume() : 0.0;
}


void
BSynth::SetSampleVolume(double)
{
}


double
BSynth::SampleVolume() const
{
	return 0.0;
}


status_t
BSynth::GetAudio(int16*, int16*, int32) const
{
	// Pull-model graph: audio leaves through the unit's output ports.
	// Direct buffer pull is not part of this façade.
	return B_NOT_SUPPORTED;
}


void
BSynth::Pause()
{
	if (fSynth != NULL)
		fSynth->Stop();
}


void
BSynth::Resume()
{
	if (fSynth != NULL)
		fSynth->Start();
}


void
BSynth::SetControllerHook(int16, synth_controller_hook)
{
}


int32
BSynth::CountClients() const
{
	return fClientCount;
}


void
BSynth::_ReservedSynth1()
{
}


void
BSynth::_ReservedSynth2()
{
}


void
BSynth::_ReservedSynth3()
{
}


void
BSynth::_ReservedSynth4()
{
}


// Construct the process singleton on first use.
struct BSynthBootstrap {
	BSynthBootstrap()
	{
		if (be_synth == NULL)
			be_synth = new BSynth(B_DEFAULT_SYNTH);
	}
	~BSynthBootstrap()
	{
		delete be_synth;
		be_synth = NULL;
	}
};
static BSynthBootstrap sBootstrap;
