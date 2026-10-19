/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/VideoFilter.h>

#include <math.h>
#include <new>
#include <stdio.h>
#include <string.h>

#include "FilterUnitCore.h"


class BVideoFilterUnit::_Input : public BMediaInput {
public:
								_Input(BVideoFilterUnit* owner)
									:
									BMediaConnection(B_MEDIA_INPUT, "in"),
									BMediaInput("in"),
									fOwner(owner)
	{
		SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_VIDEO));
	}

	virtual						~_Input() {}

protected:
	virtual	status_t			AcceptFormat(BMediaFormat* format)
	{
		if (format == NULL || !format->IsRawVideo())
			return B_BAD_VALUE;
		return B_OK;
	}

	virtual	void				HandleBuffer(void* buffer, size_t size,
									const BMediaFormat& format)
	{
		fOwner->_HandleBuffer(buffer, size, format);
	}

private:
			BVideoFilterUnit*	fOwner;
};


class BVideoFilterUnit::_Output : public BMediaOutput {
public:
								_Output()
									:
									BMediaConnection(B_MEDIA_OUTPUT, "out"),
									BMediaOutput("out")
	{
		SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_VIDEO));
	}

	virtual						~_Output() {}

protected:
	virtual	status_t			PrepareToConnect(BMediaFormat* format)
	{
		if (format == NULL || !format->IsRawVideo())
			return B_BAD_VALUE;
		return B_OK;
	}

	virtual	status_t			FormatProposal(BMediaFormat* /*format*/)
	{
		return B_OK;
	}
};


BVideoFilterUnit::BVideoFilterUnit(const char* name,
	const char* description)
	:
	BMediaClient(name != NULL ? name : "VideoFilter",
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
		name != NULL ? name : "VideoFilter", description,
		B_MEDIA_TYPE_BIT(B_MEDIA_RAW_VIDEO), &BVideoFilterUnit::_ForwardThunk,
		this);
}


BVideoFilterUnit::~BVideoFilterUnit()
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
BVideoFilterUnit::InitCheck() const
{
	if (fCore == NULL)
		return B_NO_MEMORY;
	return ((BPrivate::media::FilterUnitCore*)fCore)->InitCheck();
}


BMediaInput*
BVideoFilterUnit::Input() const
{
	return fInput;
}


BMediaOutput*
BVideoFilterUnit::Output() const
{
	return fOutput;
}


bool
BVideoFilterUnit::IsInProcessRouted() const
{
	return true;
}


int32
BVideoFilterUnit::CountParameters() const
{
	if (fCore == NULL)
		return 0;
	return ((BPrivate::media::FilterUnitCore*)fCore)->CountParameters();
}


status_t
BVideoFilterUnit::GetParameterInfo(int32 index, BString* outName,
	float* outMin, float* outMax, float* outValue)
{
	if (fCore == NULL)
		return B_NO_INIT;
	return ((BPrivate::media::FilterUnitCore*)fCore)->GetParameterInfo(index,
		outName, outMin, outMax, outValue);
}


status_t
BVideoFilterUnit::SetParameterValue(int32 index, float value)
{
	if (fCore == NULL)
		return B_NO_INIT;
	return ((BPrivate::media::FilterUnitCore*)fCore)->SetParameterValue(index,
		value);
}


float
BVideoFilterUnit::ParameterValue(int32 index) const
{
	if (fCore == NULL)
		return 0.0f;
	return ((BPrivate::media::FilterUnitCore*)fCore)->ParameterValue(index);
}


void
BVideoFilterUnit::ProcessFrame(void* /*buffer*/, size_t /*size*/,
	const BMediaFormat& /*format*/)
{
}


void
BVideoFilterUnit::_HandleBuffer(void* buffer, size_t size,
	const BMediaFormat& format)
{
	if (fCore == NULL)
		return;
	fLastFormat = format;
	((BPrivate::media::FilterUnitCore*)fCore)->ProcessBuffer(buffer, size,
		format, &BVideoFilterUnit::_CpuThunk, this);
}


/* static */ void
BVideoFilterUnit::_ForwardThunk(void* cookie, void* buffer, size_t size,
	const BMediaFormat& format)
{
	BVideoFilterUnit* self = (BVideoFilterUnit*)cookie;
	if (self->fOutput != NULL)
		self->fOutput->SendBuffer(buffer, size);
}


/* static */ void
BVideoFilterUnit::_CpuThunk(void* buffer, size_t size, void* cookie)
{
	BVideoFilterUnit* self = (BVideoFilterUnit*)cookie;
	self->ProcessFrame(buffer, size, self->fLastFormat);
}


static float
_FindParameter(BVideoFilterUnit* filter, const char* name)
{
	for (int32 i = 0; i < filter->CountParameters(); i++) {
		BString paramName;
		if (filter->GetParameterInfo(i, &paramName, NULL, NULL, NULL) != B_OK)
			continue;
		if (paramName == name)
			return (float)i;
	}
	return -1.0f;
}


static void
_SetNamedParameter(BVideoFilterUnit* filter, const char* name, float value)
{
	float index = _FindParameter(filter, name);
	if (index >= 0.0f)
		filter->SetParameterValue((int32)index, value);
}


BVideoBalanceFilter::BVideoBalanceFilter()
	:
	BVideoFilterUnit("Balance", "videobalance")
{
}


void
BVideoBalanceFilter::SetBrightness(float value)
{
	_SetNamedParameter(this, "brightness", value);
}


void
BVideoBalanceFilter::SetContrast(float value)
{
	_SetNamedParameter(this, "contrast", value);
}


void
BVideoBalanceFilter::SetSaturation(float value)
{
	_SetNamedParameter(this, "saturation", value);
}


BGrayscaleFilter::BGrayscaleFilter()
	:
	BVideoFilterUnit("Grayscale", "videobalance saturation=0")
{
}


BSepiaFilter::BSepiaFilter()
	:
	BVideoFilterUnit("Sepia", "coloreffects preset=sepia")
{
}


BInvertFilter::BInvertFilter()
	:
	BVideoFilterUnit("Invert", NULL)
{
}


void
BInvertFilter::ProcessFrame(void* buffer, size_t size,
	const BMediaFormat& /*format*/)
{
	uint8* bytes = (uint8*)buffer;
	for (size_t i = 0; i + 3 < size; i += 4) {
		bytes[i] = (uint8)(255 - bytes[i]);
		bytes[i + 1] = (uint8)(255 - bytes[i + 1]);
		bytes[i + 2] = (uint8)(255 - bytes[i + 2]);
	}
}


BWarmFilter::BWarmFilter()
	:
	BVideoFilterUnit("Warm",
		"videobalance hue=-0.12 saturation=1.15 gamma=1.05")
{
}


BCoolFilter::BCoolFilter()
	:
	BVideoFilterUnit("Cool",
		"videobalance hue=0.12 saturation=1.05 gamma=0.95")
{
}


BVintageFilter::BVintageFilter()
	:
	BVideoFilterUnit("Vintage",
		"coloreffects preset=sepia ! videobalance contrast=0.85"
		" saturation=1.2")
{
}


BVignetteFilter::BVignetteFilter()
	:
	BVideoFilterUnit("Vignette", NULL)
{
}


void
BVignetteFilter::ProcessFrame(void* buffer, size_t size,
	const BMediaFormat& format)
{
	const uint32 width = format.format.Width();
	const uint32 height = format.format.Height();
	if (width == 0 || height == 0)
		return;

	const float centerX = (float)width * 0.5f;
	const float centerY = (float)height * 0.5f;
	const float maxDistance = sqrtf(centerX * centerX + centerY * centerY);
	if (maxDistance <= 0.0f)
		return;

	uint8* bytes = (uint8*)buffer;
	for (uint32 y = 0; y < height; y++) {
		const float dy = (float)y - centerY;
		for (uint32 x = 0; x < width; x++) {
			const float dx = (float)x - centerX;
			const float distance = sqrtf(dx * dx + dy * dy);
			float falloff = distance / maxDistance;
			falloff *= falloff;
			const float factor = 1.0f - 0.55f * falloff;
			size_t offset = ((size_t)y * width + x) * 4;
			if (offset + 3 >= size)
				return;
			bytes[offset] = (uint8)(bytes[offset] * factor);
			bytes[offset + 1] = (uint8)(bytes[offset + 1] * factor);
			bytes[offset + 2] = (uint8)(bytes[offset + 2] * factor);
		}
	}
}


BBlurFilter::BBlurFilter()
	:
	BVideoFilterUnit("Blur", "gaussianblur sigma=3.0")
{
}


void
BBlurFilter::SetSigma(float sigma)
{
	_SetNamedParameter(this, "sigma", sigma);
}


BSharpenFilter::BSharpenFilter()
	:
	BVideoFilterUnit("Sharpen", "gaussianblur sigma=-1.5")
{
}


BVideoFlipFilter::BVideoFlipFilter(bool horizontal)
	:
	BVideoFilterUnit("Flip",
		horizontal ? "videoflip method=horizontal-flip" : "videoflip")
{
}
