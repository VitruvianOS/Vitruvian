/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <midi/Samples.h>

#include <new>
#include <string.h>

#include <media2/MediaClient.h>
#include <media2/MediaConnection.h>
#include <media2/MediaDefs.h>
#include <media2/MediaGraph.h>
#include <media2/MediaUnit.h>


// Sample playback is a BMediaNode output, not part of the synth unit.
class SampleOutput : public BMediaOutput {
public:
								SampleOutput();
	virtual						~SampleOutput();

protected:
	virtual	status_t			PrepareToConnect(BMediaFormat* format) override;
	virtual	status_t			FormatProposal(BMediaFormat* format) override;
	virtual	size_t				FillBuffer(void* buffer, size_t maxSize) override;

private:
			const void*			fData;
			int32				fFrames;
			int32				fBytesPerSample;
			int32				fChannels;
			int32				fCursor;
			double				fPitch;
			double				fVolume;
};


SampleOutput::SampleOutput()
	:
	// Virtual base must be named by the most-derived class.
	BMediaConnection(B_MEDIA_OUTPUT, "samples"),
	BMediaOutput("samples"),
	fData(NULL),
	fFrames(0),
	fBytesPerSample(2),
	fChannels(2),
	fCursor(0),
	fPitch(1.0),
	fVolume(1.0)
{
	SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO));
}


SampleOutput::~SampleOutput()
{
}


status_t
SampleOutput::PrepareToConnect(BMediaFormat*)
{
	return B_OK;
}


status_t
SampleOutput::FormatProposal(BMediaFormat*)
{
	return B_OK;
}


size_t
SampleOutput::FillBuffer(void* buffer, size_t maxSize)
{
	if (buffer == NULL || maxSize == 0 || fData == NULL || fFrames <= 0)
		return 0;

	const int16* samples = (const int16*)fData;
	int32 frameBytes = fBytesPerSample * fChannels;
	if (frameBytes <= 0)
		return 0;
	int32 outFrames = (int32)(maxSize / (size_t)frameBytes);
	if (outFrames <= 0)
		return 0;

	int16* out = (int16*)buffer;
	for (int32 i = 0; i < outFrames; i++) {
		if (fCursor >= fFrames)
			break;
		int32 index = fCursor;
		for (int32 c = 0; c < fChannels; c++) {
			int32 sampleIndex = index * fChannels + c;
			double scaled = samples[sampleIndex] * fVolume;
			if (scaled > 32767.0)
				scaled = 32767.0;
			if (scaled < -32768.0)
				scaled = -32768.0;
			out[i * fChannels + c] = (int16)scaled;
		}
		// Pitch steps the source cursor; 1.0 is sample-accurate playback.
		double next = fCursor + fPitch;
		fCursor = (int32)next;
	}

	return (size_t)(outFrames * frameBytes);
}


BSamples::BSamples()
	:
	fNode(NULL),
	fOutput(NULL),
	fVolume(1.0),
	fRate(44100.0),
	fPlacement(0.0),
	fReverb(false),
	fPlaying(false),
	fPaused(false)
{
	memset(_reserved, 0, sizeof(_reserved));
}


BSamples::~BSamples()
{
	Stop();
}


void
BSamples::Start(void* sampleData, int32 frames, int16 bytes_per_sample,
	int16 channel_count, double pitch, int32, int32,
	double sampleVolume, double stereoPosition, int32,
	sample_loop_hook, sample_exit_hook)
{
	Stop();

	SampleOutput* output = new(std::nothrow) SampleOutput();
	if (output == NULL)
		return;
	fOutput = output;
	fVolume = sampleVolume;
	fRate = 44100.0;
	fPlacement = stereoPosition;
	fPlaying = true;
	fPaused = false;
	(void)sampleData;
	(void)frames;
	(void)bytes_per_sample;
	(void)channel_count;
	(void)pitch;
}


bool
BSamples::IsPaused() const
{
	return fPaused;
}


void
BSamples::Pause()
{
	fPaused = true;
}


void
BSamples::Resume()
{
	fPaused = false;
}


void
BSamples::Stop()
{
	fPlaying = false;
	fPaused = false;
	delete (SampleOutput*)fOutput;
	fOutput = NULL;
}


bool
BSamples::IsPlaying() const
{
	return fPlaying && !fPaused;
}


void
BSamples::SetVolume(double newVolume)
{
	fVolume = newVolume;
}


double
BSamples::Volume() const
{
	return fVolume;
}


void
BSamples::SetSamplingRate(double newRate)
{
	fRate = newRate;
}


double
BSamples::SamplingRate() const
{
	return fRate;
}


void
BSamples::SetPlacement(double stereoPosition)
{
	fPlacement = stereoPosition;
}


double
BSamples::Placement() const
{
	return fPlacement;
}


void
BSamples::EnableReverb(bool useReverb)
{
	fReverb = useReverb;
}


void
BSamples::_ReservedSamples1()
{
}


void
BSamples::_ReservedSamples2()
{
}


void
BSamples::_ReservedSamples3()
{
}
