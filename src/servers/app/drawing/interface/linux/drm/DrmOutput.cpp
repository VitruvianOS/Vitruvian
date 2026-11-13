/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the GPL License.
 */


#include "DrmOutput.h"

#include "DrmHWInterface.h"


DrmOutput::DrmOutput(DrmHWInterface* owner, uint32 connector)
	:
	HWInterface(),
	fOwner(owner),
	fConnector(connector)
{
}


DrmOutput::~DrmOutput()
{
}


status_t
DrmOutput::Shutdown()
{
	return B_OK;
}


status_t
DrmOutput::SetMode(const display_mode& mode)
{
	return fOwner->SetOutputMode(fConnector, mode);
}


void
DrmOutput::GetMode(display_mode* mode)
{
	if (fOwner->GetOutputMode(fConnector, mode) != B_OK)
		memset(mode, 0, sizeof(display_mode));
}


status_t
DrmOutput::GetPreferredMode(display_mode* mode)
{
	return fOwner->GetOutputPreferredMode(fConnector, mode);
}


status_t
DrmOutput::GetDeviceInfo(accelerant_device_info* info)
{
	return fOwner->GetDeviceInfo(info);
}


status_t
DrmOutput::GetMonitorInfo(monitor_info* info)
{
	return fOwner->GetOutputMonitorInfo(fConnector, info);
}


status_t
DrmOutput::GetModeList(display_mode** _modeList, uint32* _count)
{
	return fOwner->GetOutputModeList(fConnector, _modeList, _count);
}


status_t
DrmOutput::GetPixelClockLimits(display_mode* mode, uint32* _low,
	uint32* _high)
{
	display_mode* modeList = NULL;
	uint32 count = 0;
	status_t status = GetModeList(&modeList, &count);
	if (status != B_OK)
		return status;
	if (count == 0) {
		delete[] modeList;
		return B_ERROR;
	}

	uint32 low = modeList[0].timing.pixel_clock;
	uint32 high = low;
	for (uint32 i = 1; i < count; i++) {
		if (modeList[i].timing.pixel_clock < low)
			low = modeList[i].timing.pixel_clock;
		if (modeList[i].timing.pixel_clock > high)
			high = modeList[i].timing.pixel_clock;
	}
	delete[] modeList;

	*_low = low;
	*_high = high;
	return B_OK;
}


status_t
DrmOutput::GetTimingConstraints(display_timing_constraints* constraints)
{
	return B_UNSUPPORTED;
}


status_t
DrmOutput::ProposeMode(display_mode* candidate, const display_mode* low,
	const display_mode* high)
{
	display_mode* modeList = NULL;
	uint32 count = 0;
	status_t status = GetModeList(&modeList, &count);
	if (status != B_OK)
		return status;

	status = DrmHWInterface::ProposeModeFromList(modeList, count, candidate,
		low, high);
	delete[] modeList;
	return status;
}


int32
DrmOutput::PanelOrientation() const
{
	return fOwner->OutputOrientation(fConnector);
}


status_t
DrmOutput::SetPanelOrientation(int32 orientation)
{
	const int32 previous = fOwner->OutputOrientation(fConnector);
	status_t status = fOwner->SetOutputOrientation(fConnector, orientation);
	if (status == B_OK && fOwner->OutputOrientation(fConnector) != previous)
		_NotifyScreenChanged();
	return status;
}


int32
DrmOutput::PanelReflection() const
{
	return fOwner->OutputReflection(fConnector);
}


status_t
DrmOutput::SetPanelReflection(int32 reflection)
{
	return fOwner->SetOutputReflection(fConnector, reflection);
}


sem_id
DrmOutput::RetraceSemaphore()
{
	return B_UNSUPPORTED;
}


status_t
DrmOutput::WaitForRetrace(bigtime_t timeout)
{
	return fOwner->WaitForRetrace(timeout);
}


status_t
DrmOutput::SetDPMSMode(uint32 state)
{
	return fOwner->SetOutputDPMSMode(fConnector, state);
}


uint32
DrmOutput::DPMSMode()
{
	return fOwner->OutputDPMSMode(fConnector);
}


uint32
DrmOutput::DPMSCapabilities()
{
	return fOwner->DPMSCapabilities();
}


status_t
DrmOutput::SetBrightness(float brightness)
{
	return B_UNSUPPORTED;
}


status_t
DrmOutput::GetBrightness(float* brightness)
{
	return B_UNSUPPORTED;
}


status_t
DrmOutput::GetConnectorName(BString& name)
{
	return fOwner->GetOutputConnectorName(fConnector, name);
}


BPoint
DrmOutput::Origin() const
{
	return fOwner->OutputOrigin(fConnector);
}


status_t
DrmOutput::SetOrigin(BPoint origin)
{
	return fOwner->SetOutputOrigin(fConnector, origin);
}


RenderingBuffer*
DrmOutput::FrontBuffer() const
{
	return fOwner->FrontBuffer();
}


RenderingBuffer*
DrmOutput::BackBuffer() const
{
	return fOwner->BackBuffer();
}


bool
DrmOutput::IsDoubleBuffered() const
{
	return fOwner->IsDoubleBuffered();
}


status_t
DrmOutput::CopyBackToFront(const BRect& frame)
{
	return fOwner->CopyBackToFront(frame);
}


status_t
DrmOutput::CopyBackToFront(const BRegion& region)
{
	return fOwner->CopyBackToFront(region);
}
