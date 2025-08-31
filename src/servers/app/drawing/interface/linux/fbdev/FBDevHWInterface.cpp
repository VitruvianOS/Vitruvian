/*
 * Copyright 2019, Dario Casalinuovo.
 * Distributed under the terms of the GPL License.
 */

#include "FBDevHWInterface.h"

#include "FBDevBuffer.h"

#include <sys/ioctl.h>
#include <sys/mman.h>

int gTTy = -1;


FBDevHWInterface::FBDevHWInterface()
	:
	HWInterface(false, false),
	fEventStream(NULL),
	fFrontBuffer(NULL),
	fBackBuffer(NULL)
{
	_InitTTy(1);

	char* fbname = getenv("FRAMEBUFFER");
	if (!fbname)
		fbname = "/dev/fb0";

	fFrameBuffer = open(fbname, O_RDWR);

	ioctl(fFrameBuffer, FBIOGET_VSCREENINFO, &fVInfo);
	ioctl(fFrameBuffer, FBIOGET_FSCREENINFO, &fInfo);

	fVInfo.grayscale = 0;
	fVInfo.bits_per_pixel = 32;
	ioctl(fFrameBuffer, FBIOPUT_VSCREENINFO, &fVInfo);
	ioctl(fFrameBuffer, FBIOGET_VSCREENINFO, &fVInfo);

	fDisplayMode.virtual_width = fVInfo.xres_virtual;
	fDisplayMode.virtual_height = fVInfo.yres_virtual;
	fDisplayMode.space = B_RGB32;

	fFrontBuffer = new FBDevBuffer(fFrameBuffer, fVInfo, fInfo);
	//fBackBuffer = new FBDevBuffer(fFrameBuffer, fVInfo, fInfo);

	ioctl(gTTy, KDSETMODE, KD_GRAPHICS);

	fEventStream = new LibInputEventStream(fVInfo.xres_virtual, fVInfo.yres_virtual);
}


FBDevHWInterface::~FBDevHWInterface()
{
	CALLED();

	_DeinitTTy();
}


status_t
FBDevHWInterface::Initialize()
{
	status_t ret = HWInterface::Initialize();
	if (ret < B_OK)
		return ret;

	ret = fFrontBuffer->InitCheck();
	if (ret < B_OK)
		return ret;

	return B_OK;
}


EventStream*
FBDevHWInterface::CreateEventStream()
{
	return fEventStream;
}


status_t
FBDevHWInterface::Shutdown()
{
	CALLED();
	return B_OK;
}


status_t
FBDevHWInterface::SetMode(const display_mode& mode)
{
	CALLED();
	return B_OK;
}


void
FBDevHWInterface::GetMode(display_mode* mode)
{
	CALLED();
	*mode = fDisplayMode;
}


status_t
FBDevHWInterface::GetDeviceInfo(accelerant_device_info* info)
{
	CALLED();
	return B_UNSUPPORTED;
}


status_t
FBDevHWInterface::GetFrameBufferConfig(frame_buffer_config& config)
{
	CALLED();
	return B_UNSUPPORTED;
}


status_t
FBDevHWInterface::GetModeList(display_mode** _modeList, uint32* _count)
{
	CALLED();
	return B_UNSUPPORTED;
}


status_t
FBDevHWInterface::GetPixelClockLimits(display_mode* mode, uint32* _low, uint32* _high)
{
	CALLED();
	return B_UNSUPPORTED;
}


status_t
FBDevHWInterface::GetTimingConstraints(display_timing_constraints* constraints)
{
	CALLED();
	return B_UNSUPPORTED;
}


status_t
FBDevHWInterface::ProposeMode(display_mode* candidate,
	const display_mode* low, const display_mode* high)
{
	CALLED();
	return B_UNSUPPORTED;
}


sem_id
FBDevHWInterface::RetraceSemaphore()
{
	CALLED();
	return B_UNSUPPORTED;
}


status_t
FBDevHWInterface::WaitForRetrace(bigtime_t timeout)
{
	CALLED();
	return B_UNSUPPORTED;
}


status_t
FBDevHWInterface::SetDPMSMode(uint32 state)
{
	CALLED();
	return B_UNSUPPORTED;
}


uint32
FBDevHWInterface::DPMSMode()
{
	CALLED();
	return B_UNSUPPORTED;
}


uint32
FBDevHWInterface::DPMSCapabilities()
{
	CALLED();
	return 0;
}


status_t
FBDevHWInterface::SetBrightness(float)
{
	CALLED();
	return B_UNSUPPORTED;
}


status_t
FBDevHWInterface::GetBrightness(float*)
{
	CALLED();
	return B_UNSUPPORTED;
}


RenderingBuffer*
FBDevHWInterface::FrontBuffer() const
{
	CALLED();
	return fFrontBuffer;
}


RenderingBuffer*
FBDevHWInterface::BackBuffer() const
{
	CALLED();
	return fBackBuffer;
}


bool
FBDevHWInterface::IsDoubleBuffered() const
{
	// TODO: This will be supported
	return false;
}


status_t
FBDevHWInterface::CopyBackToFront(const BRect& frame)
{
	CALLED();
	return B_ERROR;
}


status_t
FBDevHWInterface::_InitTTy(int ttyNumber)
{
	struct termios new_termios;

	gTTy = _OpenTTy(ttyNumber);

	//if ( ioctl(gTTy, KDSETMODE, KD_GRAPHICS) == -1 ) {
	//	printf( "Failed to switch tty mode to KD_GRAPHICS");
	//}

	// Setup keyboard
	tcgetattr(0, &fTermios);
	tcgetattr(0, &new_termios);

	new_termios.c_lflag &= ~ (ICANON);
	new_termios.c_lflag &= ~(ECHO | ECHOCTL);
	new_termios.c_iflag = 0;
	new_termios.c_cc[VMIN] = 1;
	new_termios.c_cc[VTIME] = 0;

	tcsetattr(0, TCSAFLUSH, &new_termios);

	ioctl(gTTy, VT_RELDISP, VT_ACKACQ);

	// Set-up signal handler
	struct sigaction sig_tty;
	memset(&sig_tty, 0, sizeof(sig_tty));
	sig_tty.sa_handler = _SwitchVt;
	sigemptyset(&sig_tty.sa_mask);

	if (sigaction(SIGUSR1, &sig_tty, &fSigUsr1) ||
			sigaction(SIGUSR2, &sig_tty, &fSigUsr2)) {
		goto error_signal;
	}

	struct vt_mode vt_mode;
	if (ioctl(gTTy, VT_GETMODE, &fVtMode) == -1) {
		goto error;
	}

	vt_mode = fVtMode;
	vt_mode.mode   = VT_PROCESS;
	vt_mode.waitv  = 0;
	vt_mode.relsig = SIGUSR1;
	vt_mode.acqsig = SIGUSR2;

	if (ioctl(gTTy, VT_SETMODE, &vt_mode) == -1
		|| ioctl(gTTy, VT_ACTIVATE, ttyNumber) < 0) {
		goto error;
	}

	ioctl( gTTy, VT_WAITACTIVE, ttyNumber );

	return B_OK;

error:
	sigaction(SIGUSR1, &fSigUsr1, NULL);
	sigaction(SIGUSR2, &fSigUsr2, NULL);

error_signal:
	tcsetattr(0, 0, &fTermios);

	if (ioctl(gTTy, KDSETMODE, KD_TEXT) == -1) {
		printf( "failed ioctl KDSETMODE KD_TEXT");
	}
	return B_ERROR;
}


void
FBDevHWInterface::_DeinitTTy()
{
	// Reset the terminal
	ioctl(gTTy, VT_SETMODE, &fVtMode);

	// Reset the keyboard
	tcsetattr(0, 0, &fTermios);

	if (ioctl(gTTy, KDSETMODE, KD_TEXT) == -1) {
		printf("failed ioctl KDSETMODE KD_TEXT");
	}
}


void
FBDevHWInterface::_SwitchVt(int sig)
{
	switch (sig)
	{
		case SIGUSR1:
			ioctl(gTTy, VT_RELDISP, 1);
			break;
		case SIGUSR2:
			ioctl(gTTy, VT_RELDISP, VT_ACTIVATE);
			ioctl(gTTy, VT_RELDISP, VT_ACKACQ);
			break;
	}
}


int
FBDevHWInterface::_OpenTTy(int ttyNumber)
{
	int ret;
	char filename[16];

	ret = snprintf(filename, sizeof filename, "/dev/tty%d", ttyNumber);
	if (ret < 0)
		return -1;

	return open(filename, O_RDWR);
}
