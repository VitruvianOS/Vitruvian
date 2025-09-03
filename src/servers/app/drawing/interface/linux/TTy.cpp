#include "TTy.h"

#include <stdlib.h>

int TTy::gTTy = -1;
struct termios TTy::fTermios;
struct vt_mode TTy::fVtMode;
struct sigaction TTy::fSigUsr1;
struct sigaction TTy::fSigUsr2;


status_t
TTy::InitTTy(int ttyNumber, void(*callback)(int sig))
{
	struct termios new_termios;
	gTTy = OpenTTy(ttyNumber);

	if ( ioctl(gTTy, KDSETMODE, KD_GRAPHICS) == -1 ) {
		printf( "Failed to switch tty mode to KD_GRAPHICS");
	}

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
	sig_tty.sa_handler = callback;
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
TTy::DeinitTTy()
{
	// Reset the terminal
	ioctl(gTTy, VT_SETMODE, &fVtMode);

	// Reset the keyboard
	tcsetattr(0, 0, &fTermios);

	if (ioctl(gTTy, KDSETMODE, KD_TEXT) == -1) {
		printf("failed ioctl KDSETMODE KD_TEXT");
	}
}


int
TTy::OpenTTy(int ttyNumber)
{
	int ret;
	char filename[16];

	ret = snprintf(filename, sizeof filename, "/dev/tty%d", ttyNumber);
	if (ret < 0)
		return -1;

	return open(filename, O_RDWR);
}
