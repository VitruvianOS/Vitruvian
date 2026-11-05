/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef VIRTUAL_OUTPUT_H
#define VIRTUAL_OUTPUT_H


#include "HWInterface.h"

#include <AutoDeleter.h>
#include <String.h>


class MallocBuffer;


// Headless output backed by two malloc buffers: the compositor renders
// as on any other screen; output add-ons capture the result. Used by
// remote desktop sessions, casting and recording.
class VirtualOutput : public HWInterface {
public:
								VirtualOutput(uint16 width, uint16 height,
									const char* name);
	virtual						~VirtualOutput();

	virtual	status_t			Initialize();
	virtual	status_t			Shutdown();

	virtual	status_t			SetMode(const display_mode& mode);
	virtual	void				GetMode(display_mode* mode);

	virtual	status_t			GetDeviceInfo(accelerant_device_info* info);
	virtual	status_t			GetFrameBufferConfig(
									frame_buffer_config& config);

	virtual	status_t			GetModeList(display_mode** _modeList,
									uint32* _count);
	virtual	status_t			GetPixelClockLimits(display_mode* mode,
									uint32* _low, uint32* _high);
	virtual	status_t			GetTimingConstraints(
									display_timing_constraints* constraints);
	virtual	status_t			ProposeMode(display_mode* candidate,
									const display_mode* low,
									const display_mode* high);

	virtual	sem_id				RetraceSemaphore();
	virtual	status_t			WaitForRetrace(
									bigtime_t timeout = B_INFINITE_TIMEOUT);

	virtual	status_t			SetDPMSMode(uint32 state);
	virtual	uint32				DPMSMode();
	virtual	uint32				DPMSCapabilities();

	virtual	status_t			SetBrightness(float);
	virtual	status_t			GetBrightness(float*);

	virtual	status_t			GetConnectorName(BString& name);

	virtual	RenderingBuffer*	FrontBuffer() const;
	virtual	RenderingBuffer*	BackBuffer() const;
	virtual	bool				IsDoubleBuffered() const;

	virtual	status_t			CopyBackToFront(const BRect& frame);

			// Animates a moving bar so a headless output with nothing
			// placed on it yet still produces frames to capture.
			void				SetTestPattern(bool enable);

private:
			void				_AllocBuffers(uint16 width, uint16 height);
			void				_FreeBuffers();
	static	void				_FillMode(display_mode& mode, uint16 width,
									uint16 height);
	static	int32				_TestPatternThread(void* data);
			void				_TestPatternLoop();

			uint16				fWidth;
			uint16				fHeight;
			display_mode		fMode;
			BString				fName;
			sem_id				fRetraceSem;
			uint32				fDPMSMode;
			thread_id			fPatternThread;
			volatile int32		fPatternQuit;
			ObjectDeleter<MallocBuffer>
								fFrontBuffer;
			ObjectDeleter<MallocBuffer>
								fBackBuffer;
};


#endif	// VIRTUAL_OUTPUT_H
