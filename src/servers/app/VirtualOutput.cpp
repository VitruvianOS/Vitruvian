/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "VirtualOutput.h"

#include "MallocBuffer.h"

#include <OutputAddonProtocol.h>

#include <new>
#include <string.h>

#include <Region.h>


VirtualOutput::VirtualOutput(uint16 width, uint16 height, const char* name)
	:
	fWidth(width),
	fHeight(height),
	fName(name != NULL ? name : "virtual"),
	fRetraceSem(-1),
	fDPMSMode(B_DPMS_ON),
	fPatternThread(-1),
	fPatternQuit(0)
{
	if (fWidth == 0)
		fWidth = 640;
	if (fHeight == 0)
		fHeight = 480;

	_FillMode(fMode, fWidth, fHeight);
}


VirtualOutput::~VirtualOutput()
{
	SetTestPattern(false);
	Shutdown();
}


/*static*/ void
VirtualOutput::_FillMode(display_mode& mode, uint16 width, uint16 height)
{
	memset(&mode, 0, sizeof(display_mode));
	mode.timing.pixel_clock = (uint64)width * height * 60 / 1000;
	mode.timing.h_display = width;
	mode.timing.h_sync_start = width;
	mode.timing.h_sync_end = width;
	mode.timing.h_total = width;
	mode.timing.v_display = height;
	mode.timing.v_sync_start = height;
	mode.timing.v_sync_end = height;
	mode.timing.v_total = height;
	mode.space = B_RGBA32;
	mode.virtual_width = width;
	mode.virtual_height = height;
}


void
VirtualOutput::_AllocBuffers(uint16 width, uint16 height)
{
	_FreeBuffers();

	fFrontBuffer.SetTo(new(std::nothrow) MallocBuffer(width, height));
	fBackBuffer.SetTo(new(std::nothrow) MallocBuffer(width, height));
	if (!fFrontBuffer.IsSet() || !fBackBuffer.IsSet()
		|| fFrontBuffer->InitCheck() != B_OK
		|| fBackBuffer->InitCheck() != B_OK) {
		_FreeBuffers();
		return;
	}

	fWidth = width;
	fHeight = height;
	_FillMode(fMode, width, height);
}


void
VirtualOutput::_FreeBuffers()
{
	fFrontBuffer.Unset();
	fBackBuffer.Unset();
}


status_t
VirtualOutput::Initialize()
{
	if (fRetraceSem < B_OK) {
		fRetraceSem = create_sem(0, "virtual output retrace");
		if (fRetraceSem < B_OK)
			return fRetraceSem;
	}

	if (!fFrontBuffer.IsSet())
		_AllocBuffers(fWidth, fHeight);

	return fFrontBuffer.IsSet() ? B_OK : B_NO_MEMORY;
}


status_t
VirtualOutput::Shutdown()
{
	SetTestPattern(false);

	_FreeBuffers();

	if (fRetraceSem >= B_OK) {
		delete_sem(fRetraceSem);
		fRetraceSem = -1;
	}

	return B_OK;
}


void
VirtualOutput::SetTestPattern(bool enable)
{
	if (enable) {
		if (fPatternThread >= B_OK)
			return;

		fPatternQuit = 0;
		fPatternThread = spawn_thread(_TestPatternThread,
			"virtual output pattern", B_NORMAL_PRIORITY, this);
		if (fPatternThread < B_OK) {
			fPatternThread = -1;
			return;
		}

		resume_thread(fPatternThread);
		return;
	}

	if (fPatternThread < B_OK)
		return;

	atomic_set(&fPatternQuit, 1);
	int32 status;
	wait_for_thread(fPatternThread, &status);
	fPatternThread = -1;
}


/*static*/ int32
VirtualOutput::_TestPatternThread(void* data)
{
	VirtualOutput* output = (VirtualOutput*)data;
	output->_TestPatternLoop();
	return 0;
}


void
VirtualOutput::_TestPatternLoop()
{
	int32 step = 0;
	while (atomic_get(&fPatternQuit) == 0) {
		if (!LockExclusiveAccess()) {
			snooze(20000);
			continue;
		}

		if (fBackBuffer.IsSet() && fBackBuffer->InitCheck() == B_OK) {
			uint8* bits = (uint8*)fBackBuffer->Bits();
			int32 stride = fBackBuffer->BytesPerRow();

			int32 barWidth = fWidth / 8;
			int32 barX = (step * 8) % (fWidth + barWidth) - barWidth;
			if (barX < 0)
				barX = 0;

			for (int32 y = 0; y < fHeight; y++) {
				uint8* row = bits + (int64)y * stride;
				for (int32 x = barX; x < barX + barWidth && x < fWidth; x++) {
					row[x * 4 + 0] = 0x20;
					row[x * 4 + 1] = (uint8)(64 + (step * 4) % 192);
					row[x * 4 + 2] = 0xe0;
					row[x * 4 + 3] = 0xff;
				}
			}

			BRect full(0, 0, fWidth - 1, fHeight - 1);
			Invalidate(full);
			step++;
		}

		UnlockExclusiveAccess();
		snooze(100000);
	}
}


status_t
VirtualOutput::SetMode(const display_mode& mode)
{
	if (mode.space != B_RGBA32)
		return B_BAD_VALUE;
	if (mode.virtual_width == 0 || mode.virtual_height == 0)
		return B_BAD_VALUE;
	if (mode.virtual_width > kOutputAddonMaxWidth
		|| mode.virtual_height > kOutputAddonMaxHeight) {
		return B_BAD_VALUE;
	}

	LockExclusiveAccess();
	_AllocBuffers(mode.virtual_width, mode.virtual_height);
	UnlockExclusiveAccess();

	return fFrontBuffer.IsSet() ? B_OK : B_NO_MEMORY;
}


void
VirtualOutput::GetMode(display_mode* mode)
{
	*mode = fMode;
}


status_t
VirtualOutput::GetDeviceInfo(accelerant_device_info* info)
{
	memset(info, 0, sizeof(accelerant_device_info));
	strlcpy(info->name, fName.String(), sizeof(info->name));
	strlcpy(info->chipset, "virtual", sizeof(info->chipset));
	info->memory = (uint64)fWidth * fHeight * 4 * 2;
	info->dac_speed = 400;
	return B_OK;
}


status_t
VirtualOutput::GetFrameBufferConfig(frame_buffer_config& config)
{
	if (!fFrontBuffer.IsSet())
		return B_NO_INIT;

	config.frame_buffer = fFrontBuffer->Bits();
	config.frame_buffer_dma = NULL;
	config.bytes_per_row = fFrontBuffer->BytesPerRow();
	return B_OK;
}


status_t
VirtualOutput::GetModeList(display_mode** _modeList, uint32* _count)
{
	display_mode* list = new(std::nothrow) display_mode[1];
	if (list == NULL)
		return B_NO_MEMORY;

	list[0] = fMode;
	*_modeList = list;
	*_count = 1;
	return B_OK;
}


status_t
VirtualOutput::GetPixelClockLimits(display_mode* mode, uint32* _low,
	uint32* _high)
{
	*_low = mode->timing.pixel_clock;
	*_high = mode->timing.pixel_clock;
	return B_OK;
}


status_t
VirtualOutput::GetTimingConstraints(display_timing_constraints*)
{
	return B_UNSUPPORTED;
}


status_t
VirtualOutput::ProposeMode(display_mode* candidate, const display_mode* /*low*/,
	const display_mode* /*high*/)
{
	if (candidate->space != B_RGBA32)
		candidate->space = B_RGBA32;

	if (candidate->virtual_width == 0 || candidate->virtual_height == 0) {
		candidate->virtual_width = candidate->timing.h_display;
		candidate->virtual_height = candidate->timing.v_display;
	}

	if (candidate->virtual_width == 0 || candidate->virtual_height == 0)
		return B_BAD_VALUE;

	_FillMode(*candidate, candidate->virtual_width, candidate->virtual_height);
	return B_OK;
}


sem_id
VirtualOutput::RetraceSemaphore()
{
	return fRetraceSem;
}


status_t
VirtualOutput::WaitForRetrace(bigtime_t timeout)
{
	status_t status;
	do {
		status = acquire_sem_etc(fRetraceSem, 1, B_RELATIVE_TIMEOUT, timeout);
	} while (status == B_INTERRUPTED);

	return status;
}


status_t
VirtualOutput::SetDPMSMode(uint32 state)
{
	if (state != B_DPMS_ON && state != B_DPMS_STAND_BY
		&& state != B_DPMS_SUSPEND && state != B_DPMS_OFF) {
		return B_BAD_VALUE;
	}

	fDPMSMode = state;
	return B_OK;
}


uint32
VirtualOutput::DPMSMode()
{
	return fDPMSMode;
}


uint32
VirtualOutput::DPMSCapabilities()
{
	return B_DPMS_STAND_BY | B_DPMS_SUSPEND | B_DPMS_OFF;
}


status_t
VirtualOutput::SetBrightness(float)
{
	return B_UNSUPPORTED;
}


status_t
VirtualOutput::GetBrightness(float*)
{
	return B_UNSUPPORTED;
}


status_t
VirtualOutput::GetConnectorName(BString& name)
{
	name = fName;
	return B_OK;
}


RenderingBuffer*
VirtualOutput::FrontBuffer() const
{
	return fFrontBuffer.Get();
}


RenderingBuffer*
VirtualOutput::BackBuffer() const
{
	return fBackBuffer.Get();
}


bool
VirtualOutput::IsDoubleBuffered() const
{
	return fBackBuffer.IsSet();
}


status_t
VirtualOutput::CopyBackToFront(const BRect& frame)
{
	if (!fFrontBuffer.IsSet() || !fBackBuffer.IsSet())
		return B_NO_INIT;

	IntRect area(frame);
	IntRect clip(0, 0, fFrontBuffer->Width() - 1, fFrontBuffer->Height() - 1);
	area = clip & area;
	if (!area.IsValid())
		return B_BAD_VALUE;

	const uint8* srcBase = (const uint8*)fBackBuffer->Bits();
	uint8* dstBase = (uint8*)fFrontBuffer->Bits();
	int32 srcStride = fBackBuffer->BytesPerRow();
	int32 dstStride = fFrontBuffer->BytesPerRow();
	int32 width = area.Width();

	for (int32 y = area.top; y <= area.bottom; y++) {
		const uint8* src = srcBase + (int64)y * srcStride + (int64)area.left * 4;
		uint8* dst = dstBase + (int64)y * dstStride + (int64)area.left * 4;
		memcpy(dst, src, (size_t)width * 4);
	}

	if (fRetraceSem >= B_OK)
		release_sem(fRetraceSem);

	return B_OK;
}
