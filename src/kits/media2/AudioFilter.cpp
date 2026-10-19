/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/AudioFilter.h>

#include <new>
#include <stdio.h>
#include <string.h>

#include "FilterUnitCore.h"


class BAudioFilterUnit::_Input : public BMediaInput {
public:
								_Input(BAudioFilterUnit* owner)
									:
									BMediaConnection(B_MEDIA_INPUT, "in"),
									BMediaInput("in"),
									fOwner(owner)
	{
		SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO));
	}

	virtual						~_Input() {}

protected:
	virtual	status_t			AcceptFormat(BMediaFormat* format)
	{
		if (format == NULL || !format->IsRawAudio())
			return B_BAD_VALUE;
		return B_OK;
	}

	virtual	void				HandleBuffer(void* buffer, size_t size,
									const BMediaFormat& format)
	{
		fOwner->_HandleBuffer(buffer, size, format);
	}

private:
			BAudioFilterUnit*	fOwner;
};


class BAudioFilterUnit::_Output : public BMediaOutput {
public:
								_Output()
									:
									BMediaConnection(B_MEDIA_OUTPUT, "out"),
									BMediaOutput("out")
	{
		SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO));
	}

	virtual						~_Output() {}

protected:
	virtual	status_t			PrepareToConnect(BMediaFormat* format)
	{
		if (format == NULL || !format->IsRawAudio())
			return B_BAD_VALUE;
		return B_OK;
	}

	virtual	status_t			FormatProposal(BMediaFormat* /*format*/)
	{
		return B_OK;
	}
};


BAudioFilterUnit::BAudioFilterUnit(const char* name,
	const char* description)
	:
	BMediaClient(name != NULL ? name : "AudioFilter",
		(media_client_kinds)(B_MEDIA_FILTER | B_MEDIA_CONTROLLABLE)),
	fCore(NULL),
	fInput(NULL),
	fOutput(NULL)
{
	fInput = new(std::nothrow) _Input(this);
	fOutput = new(std::nothrow) _Output();
	if (fInput == NULL || fOutput == NULL) {
		delete fInput;
		delete fOutput;
		fInput = NULL;
		fOutput = NULL;
		return;
	}

	if (RegisterInput(fInput) != B_OK || RegisterOutput(fOutput) != B_OK) {
		delete fInput;
		delete fOutput;
		fInput = NULL;
		fOutput = NULL;
		return;
	}

	fCore = new(std::nothrow) BPrivate::media::FilterUnitCore(
		name != NULL ? name : "AudioFilter", description,
		B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO), &BAudioFilterUnit::_ForwardThunk,
		this);
}


BAudioFilterUnit::~BAudioFilterUnit()
{
	if (fInput != NULL) {
		UnregisterInput(fInput);
		delete fInput;
		fInput = NULL;
	}
	if (fOutput != NULL) {
		UnregisterOutput(fOutput);
		delete fOutput;
		fOutput = NULL;
	}
	delete (BPrivate::media::FilterUnitCore*)fCore;
	fCore = NULL;
}


status_t
BAudioFilterUnit::InitCheck() const
{
	if (fCore == NULL)
		return B_NO_MEMORY;
	return ((BPrivate::media::FilterUnitCore*)fCore)->InitCheck();
}


BMediaInput*
BAudioFilterUnit::Input() const
{
	return fInput;
}


BMediaOutput*
BAudioFilterUnit::Output() const
{
	return fOutput;
}


bool
BAudioFilterUnit::IsInProcessRouted() const
{
	return true;
}


int32
BAudioFilterUnit::CountParameters() const
{
	if (fCore == NULL)
		return 0;
	return ((BPrivate::media::FilterUnitCore*)fCore)->CountParameters();
}


status_t
BAudioFilterUnit::GetParameterInfo(int32 index, BString* outName,
	float* outMin, float* outMax, float* outValue)
{
	if (fCore == NULL)
		return B_NO_INIT;
	return ((BPrivate::media::FilterUnitCore*)fCore)->GetParameterInfo(index,
		outName, outMin, outMax, outValue);
}


status_t
BAudioFilterUnit::SetParameterValue(int32 index, float value)
{
	if (fCore == NULL)
		return B_NO_INIT;
	return ((BPrivate::media::FilterUnitCore*)fCore)->SetParameterValue(index,
		value);
}


float
BAudioFilterUnit::ParameterValue(int32 index) const
{
	if (fCore == NULL)
		return 0.0f;
	return ((BPrivate::media::FilterUnitCore*)fCore)->ParameterValue(index);
}


void
BAudioFilterUnit::_HandleBuffer(void* buffer, size_t size,
	const BMediaFormat& format)
{
	if (fCore == NULL)
		return;
	((BPrivate::media::FilterUnitCore*)fCore)->ProcessBuffer(buffer, size,
		format, NULL, NULL);
}


/* static */ void
BAudioFilterUnit::_ForwardThunk(void* cookie, void* buffer, size_t size,
	const BMediaFormat& /*format*/)
{
	BAudioFilterUnit* self = (BAudioFilterUnit*)cookie;
	if (self->fOutput != NULL)
		self->fOutput->SendBuffer(buffer, size);
}


BEqualizerFilter::BEqualizerFilter()
	:
	BAudioFilterUnit("Equalizer", "equalizer-10bands")
{
}


BGainFilter::BGainFilter()
	:
	BAudioFilterUnit("Gain", "volume")
{
}


void
BGainFilter::SetGain(float linear)
{
	for (int32 i = 0; i < CountParameters(); i++) {
		BString name;
		if (GetParameterInfo(i, &name, NULL, NULL, NULL) != B_OK)
			continue;
		if (name == "volume") {
			SetParameterValue(i, linear);
			break;
		}
	}
}


BCompressorFilter::BCompressorFilter()
	:
	BAudioFilterUnit("Compressor", "audiodynamic")
{
}


BReverbFilter::BReverbFilter()
	:
	BAudioFilterUnit("Reverb", "freeverb")
{
}
