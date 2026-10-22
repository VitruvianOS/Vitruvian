/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <media2/MediaFilter.h>

#include <new>
#include <stdio.h>
#include <string.h>

#include <OS.h>

#include <media2/Controllable.h>
#include <media2/ParameterWeb.h>

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
	virtual	void				Connected(const BMediaFormat& format)
	{
		BMediaInput::Connected(format);
		// Video keeps the frame layout; the output offers what came in.
		if (fOwner->fType == B_MEDIA_RAW_VIDEO && fOwner->Output() != NULL)
			fOwner->Output()->SetFormat(format);
	}

private:
			BMediaFilter*	fOwner;
};


BMediaFilter::BMediaFilter(const char* name, const char* description,
	media_type type)
	:
	BMediaUnit(name != NULL ? name : "MediaFilter",
		(media_client_kinds)(B_MEDIA_FILTER | B_MEDIA_CONTROLLABLE)),
	fType(type),
	fCore(NULL),
	fInput(NULL),
	fOutput(NULL),
	fControllable(NULL)
{
	SetDetachedOnly(true);
	fInput = new(std::nothrow) _Input(this);
	fOutput = new(std::nothrow) BMediaOutput("out");
	if (fInput == NULL || fOutput == NULL) {
		delete fInput;
		delete fOutput;
		fInput = NULL;
		fOutput = NULL;
		return;
	}

	fOutput->SetAcceptedTypes(B_MEDIA_TYPE_BIT(type));
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
	if (fCore != NULL)
		_BuildWeb();
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
	delete fControllable;
	fControllable = NULL;
	delete (BPrivate::media::FilterUnitCore*)fCore;
	fCore = NULL;
}


// The GStreamer properties become one control each, so MakeView() and
// Cortex's Control panel work with no app code.
void
BMediaFilter::_BuildWeb()
{
	BPrivate::media::FilterUnitCore* core
		= (BPrivate::media::FilterUnitCore*)fCore;
	if (core == NULL || core->InitCheck() != B_OK)
		return;

	BParameterWeb* web = new(std::nothrow) BParameterWeb();
	if (web == NULL)
		return;

	BParameterGroup* group = web->MakeGroup(Name());
	if (group == NULL) {
		delete web;
		return;
	}
	core->BuildParameterWeb(group);

	fControllable = new(std::nothrow) BControllable();
	if (fControllable == NULL) {
		delete web;
		return;
	}
	fControllable->SetParameterWeb(web);
	fControllable->SetChangeHook(&BMediaFilter::_ChangeHook, this);
	SetControllable(fControllable);

	// Seed the stored values with what the chain holds now.
	const int32 count = core->CountParameters();
	for (int32 i = 0; i < count; i++) {
		const float value = core->ParameterValue(i);
		fControllable->SetParameterValue(i, system_time(), &value,
			sizeof(value));
	}
}


/* static */ void
BMediaFilter::_ChangeHook(int32 id, bigtime_t /*when*/, const void* value,
	size_t size, void* cookie)
{
	BMediaFilter* self = (BMediaFilter*)cookie;
	if (self == NULL || value == NULL || size == 0)
		return;

	float floatValue = 0.0f;
	if (size == sizeof(int32))
		floatValue = (float)*(const int32*)value;
	else if (size >= sizeof(float))
		floatValue = *(const float*)value;
	else
		return;
	self->SetParameterValue(id, floatValue);
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


void
BMediaFilter::ProcessCycle(uint32 /*frameCount*/)
{
	size_t size = 0;
	void* buffer = BufferFor(fInput, &size);
	if (buffer != NULL)
		_HandleBuffer(buffer, size, CycleFormat());
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
