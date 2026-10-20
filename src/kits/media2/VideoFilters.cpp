/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <media2/MediaFilter.h>

#include <media2/MediaGraph.h>

#include <Autolock.h>
#include <math.h>
#include <new>
#include <string.h>


static float
_FindParameter(BMediaFilter* filter, const char* name)
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
_SetNamedParameter(BMediaFilter* filter, const char* name, float value)
{
	float index = _FindParameter(filter, name);
	if (index >= 0.0f)
		filter->SetParameterValue((int32)index, value);
}


BVideoBalanceFilter::BVideoBalanceFilter()
	:
	BMediaFilter("Balance", "videobalance", B_MEDIA_RAW_VIDEO)
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
	BMediaFilter("Grayscale", "videobalance saturation=0",
		B_MEDIA_RAW_VIDEO)
{
}


BSepiaFilter::BSepiaFilter()
	:
	BMediaFilter("Sepia", "coloreffects preset=sepia", B_MEDIA_RAW_VIDEO)
{
}


BInvertFilter::BInvertFilter()
	:
	BMediaFilter("Invert", NULL, B_MEDIA_RAW_VIDEO)
{
}


void
BInvertFilter::ProcessBuffer(void* buffer, size_t size,
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
	BMediaFilter("Warm",
		"videobalance hue=-0.12 saturation=1.15 brightness=0.03",
		B_MEDIA_RAW_VIDEO)
{
}


BCoolFilter::BCoolFilter()
	:
	BMediaFilter("Cool",
		"videobalance hue=0.12 saturation=1.05 brightness=-0.02",
		B_MEDIA_RAW_VIDEO)
{
}


BVintageFilter::BVintageFilter()
	:
	BMediaFilter("Vintage",
		"coloreffects preset=sepia ! videobalance contrast=0.85"
		" saturation=1.2", B_MEDIA_RAW_VIDEO)
{
}


BVignetteFilter::BVignetteFilter()
	:
	BMediaFilter("Vignette", NULL, B_MEDIA_RAW_VIDEO)
{
}


void
BVignetteFilter::ProcessBuffer(void* buffer, size_t size,
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
	BMediaFilter("Blur", "gaussianblur sigma=3.0", B_MEDIA_RAW_VIDEO)
{
}


void
BBlurFilter::SetSigma(float sigma)
{
	_SetNamedParameter(this, "sigma", sigma);
}


BSharpenFilter::BSharpenFilter()
	:
	BMediaFilter("Sharpen", "gaussianblur sigma=-1.5", B_MEDIA_RAW_VIDEO)
{
}


BVideoFlipFilter::BVideoFlipFilter(bool horizontal)
	:
	BMediaFilter("Flip",
		horizontal ? "videoflip method=horizontal-flip" : "videoflip",
		B_MEDIA_RAW_VIDEO)
{
}


class BVideoSplitter::_Input : public BMediaInput {
public:
								_Input(BVideoSplitter* owner)
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

	virtual	void				Connected(const BMediaFormat& format)
	{
		BMediaInput::Connected(format);
		BAutolock _(fOwner->fLock);
		for (int32 i = 0; i < fOwner->fOutputs->CountItems(); i++)
			fOwner->fOutputs->ItemAt(i)->SetFormat(format);
	}

	virtual	void				HandleBuffer(void* buffer, size_t size,
									const BMediaFormat& format)
	{
		BAutolock _(fOwner->fLock);
		for (int32 i = 0; i < fOwner->fOutputs->CountItems(); i++) {
			BMediaOutput* output = fOwner->fOutputs->ItemAt(i);
			if (output != NULL && output->IsConnected()) {
				// Frames carry their own shape; it changes with the mode.
				output->SetFormat(format);
				output->SendBuffer(buffer, size);
			}
		}
	}

private:
			BVideoSplitter*		fOwner;
};


class BVideoSplitter::_Output : public BMediaOutput {
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


BVideoSplitter::BVideoSplitter()
	:
	BMediaClient("Splitter",
		(media_client_kinds)(B_MEDIA_FILTER | B_MEDIA_VIRTUAL)),
	fInput(NULL),
	fOutputs(NULL),
	fLock("video splitter")
{
	fInput = new(std::nothrow) _Input(this);
	fOutputs = new(std::nothrow) BObjectList<BMediaOutput, false>();
	if (fInput == NULL || fOutputs == NULL) {
		delete fInput;
		delete fOutputs;
		fInput = NULL;
		fOutputs = NULL;
		return;
	}
	RegisterInput(fInput);
}


BVideoSplitter::~BVideoSplitter()
{
	if (fInput != NULL) {
		UnregisterInput(fInput);
		delete fInput;
	}
	while (fOutputs->CountItems() > 0) {
		BMediaOutput* output = fOutputs->RemoveItemAt(0);
		UnregisterOutput(output);
		delete output;
	}
	delete fOutputs;
}


bool
BVideoSplitter::IsInProcessRouted() const
{
	return true;
}


BMediaInput*
BVideoSplitter::Input() const
{
	return fInput;
}


BMediaOutput*
BVideoSplitter::AddOutput()
{
	BAutolock _(fLock);
	_Output* output = new(std::nothrow) _Output();
	if (output == NULL)
		return NULL;
	if (RegisterOutput(output) != B_OK) {
		delete output;
		return NULL;
	}
	if (fInput->IsConnected())
		output->SetFormat(fInput->Format());
	fOutputs->AddItem(output);
	return output;
}


int32
BVideoSplitter::CountOutputs() const
{
	BAutolock _(fLock);
	return fOutputs != NULL ? fOutputs->CountItems() : 0;
}


BMediaOutput*
BVideoSplitter::OutputAt(int32 index) const
{
	BAutolock _(fLock);
	return fOutputs != NULL ? fOutputs->ItemAt(index) : NULL;
}


status_t
BVideoSplitter::RemoveOutput(BMediaOutput* output)
{
	if (output == NULL)
		return B_BAD_VALUE;
	{
		BAutolock _(fLock);
		if (fOutputs == NULL || fOutputs->RemoveItem(output) == false)
			return B_ENTRY_NOT_FOUND;
	}
	BMediaGraph* graph = BMediaGraph::Instance();
	BMediaInput* input = dynamic_cast<BMediaInput*>(output->Binding());
	if (graph != NULL && input != NULL && output->IsConnected())
		graph->Disconnect(output, input);
	UnregisterOutput(output);
	delete output;
	return B_OK;
}
