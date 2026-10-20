/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <media2/MediaFilter.h>

#include <new>
#include <stdio.h>
#include <string.h>

#include "FilterUnitCore.h"


class BMediaFilter::_Input : public BMediaInput {
public:
								_Input(BMediaFilter* owner)
									:
									BMediaConnection(B_MEDIA_INPUT, "in"),
									BMediaInput("in"),
									fOwner(owner)
	{
		SetAcceptedTypes(B_MEDIA_TYPE_BIT(owner->fType));
	}

	virtual						~_Input() {}

protected:
	virtual	status_t			AcceptFormat(BMediaFormat* format)
	{
		if (format == NULL || format->Type() != fOwner->fType)
			return B_BAD_VALUE;
		return B_OK;
	}

	virtual	void				Connected(const BMediaFormat& format)
	{
		BMediaInput::Connected(format);
		// Video keeps the frame layout; the output offers what came in.
		if (fOwner->fType == B_MEDIA_RAW_VIDEO && fOwner->Output() != NULL)
			fOwner->Output()->SetFormat(format);
	}

	virtual	void				HandleBuffer(void* buffer, size_t size,
									const BMediaFormat& format)
	{
		fOwner->_HandleBuffer(buffer, size, format);
	}

private:
			BMediaFilter*	fOwner;
};


class BMediaFilter::_Output : public BMediaOutput {
public:
								_Output(media_type type)
									:
									BMediaConnection(B_MEDIA_OUTPUT, "out"),
									BMediaOutput("out"),
									fType(type)
	{
		SetAcceptedTypes(B_MEDIA_TYPE_BIT(type));
	}

	virtual						~_Output() {}

protected:
	virtual	status_t			PrepareToConnect(BMediaFormat* format)
	{
		if (format == NULL || format->Type() != fType)
			return B_BAD_VALUE;
		return B_OK;
	}

	virtual	status_t			FormatProposal(BMediaFormat* /*format*/)
	{
		return B_OK;
	}

private:
			media_type			fType;
};


BMediaFilter::BMediaFilter(const char* name, const char* description,
	media_type type)
	:
	BMediaClient(name != NULL ? name : "MediaFilter",
		(media_client_kinds)(B_MEDIA_FILTER | B_MEDIA_CONTROLLABLE)),
	fType(type),
	fCore(NULL),
	fInput(NULL),
	fOutput(NULL)
{
	fInput = new(std::nothrow) _Input(this);
	fOutput = new(std::nothrow) _Output(type);
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
		name != NULL ? name : "MediaFilter", description,
		B_MEDIA_TYPE_BIT(type), &BMediaFilter::_ForwardThunk, this);
}


BMediaFilter::~BMediaFilter()
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
BMediaFilter::InitCheck() const
{
	if (fCore == NULL)
		return B_NO_MEMORY;
	return ((BPrivate::media::FilterUnitCore*)fCore)->InitCheck();
}


media_type
BMediaFilter::Type() const
{
	return fType;
}


BMediaInput*
BMediaFilter::Input() const
{
	return fInput;
}


BMediaOutput*
BMediaFilter::Output() const
{
	return fOutput;
}


bool
BMediaFilter::IsInProcessRouted() const
{
	return true;
}


int32
BMediaFilter::CountParameters() const
{
	if (fCore == NULL)
		return 0;
	return ((BPrivate::media::FilterUnitCore*)fCore)->CountParameters();
}


status_t
BMediaFilter::GetParameterInfo(int32 index, BString* outName,
	float* outMin, float* outMax, float* outValue)
{
	if (fCore == NULL)
		return B_NO_INIT;
	return ((BPrivate::media::FilterUnitCore*)fCore)->GetParameterInfo(index,
		outName, outMin, outMax, outValue);
}


status_t
BMediaFilter::SetParameterValue(int32 index, float value)
{
	if (fCore == NULL)
		return B_NO_INIT;
	return ((BPrivate::media::FilterUnitCore*)fCore)->SetParameterValue(index,
		value);
}


float
BMediaFilter::ParameterValue(int32 index) const
{
	if (fCore == NULL)
		return 0.0f;
	return ((BPrivate::media::FilterUnitCore*)fCore)->ParameterValue(index);
}


void
BMediaFilter::ProcessBuffer(void* /*buffer*/, size_t /*size*/,
	const BMediaFormat& /*format*/)
{
}


void
BMediaFilter::_HandleBuffer(void* buffer, size_t size,
	const BMediaFormat& format)
{
	if (fCore == NULL)
		return;
	fLastFormat = format;
	((BPrivate::media::FilterUnitCore*)fCore)->ProcessBuffer(buffer, size,
		format, &BMediaFilter::_CpuThunk, this);
}


/* static */ void
BMediaFilter::_ForwardThunk(void* cookie, void* buffer, size_t size,
	const BMediaFormat& format)
{
	BMediaFilter* self = (BMediaFilter*)cookie;
	if (self->fOutput == NULL)
		return;
	if (self->fType == B_MEDIA_RAW_VIDEO)
		self->fOutput->SetFormat(format);
	self->fOutput->SendBuffer(buffer, size);
}


/* static */ void
BMediaFilter::_CpuThunk(void* buffer, size_t size, void* cookie)
{
	BMediaFilter* self = (BMediaFilter*)cookie;
	self->ProcessBuffer(buffer, size, self->fLastFormat);
}
