/*
 * Copyright 2021-2026, Dario Casalinuovo.
 * Distributed under the terms of the GPL License.
 */

#include "TTy.h"

#include <termios.h>
#include <sys/stat.h>

int TTy::gTTy = -1;
struct termios TTy::fTermios;


status_t
TTy::InitTTy(int ttyNumber, void(*callback)(int sig))
{
	struct termios new_termios;
	gTTy = OpenTTy(ttyNumber);
	if (gTTy < 0)
		return B_ERROR;

	int termfd = gTTy;

	if (!isatty(termfd)) {
		fprintf(stderr, "not a tty: /dev/tty%d\n", ttyNumber);
		close(gTTy);
		gTTy = -1;
		return B_ERROR;
	}

	if (tcgetattr(termfd, &fTermios) == -1) {
		perror("tcgetattr");
		close(gTTy);
		gTTy = -1;
		return B_ERROR;
	}

	new_termios = fTermios;

	new_termios.c_lflag &= ~ICANON;
	new_termios.c_lflag &= ~(ECHO | ECHOCTL);
	new_termios.c_iflag = 0;
	new_termios.c_oflag = 0;
	new_termios.c_cc[VMIN] = 1;
	new_termios.c_cc[VTIME] = 0;

	if (tcsetattr(termfd, TCSAFLUSH, &new_termios) == -1) {
		perror("tcsetattr");
		tcsetattr(termfd, TCSANOW, &fTermios);
		close(gTTy);
		gTTy = -1;
		return B_ERROR;
	}

	return B_OK;
}


void
TTy::DeinitTTy()
{
	if (gTTy >= 0) {
		int termfd = gTTy;

		if (isatty(termfd)) {
			if (tcsetattr(termfd, TCSANOW, &fTermios) == -1) {
				perror("tcsetattr restore");
			}
		}

		close(gTTy);
		gTTy = -1;
	}
}


int
TTy::OpenTTy(int ttyNumber)
{
	char filename[16];
	int ret = snprintf(filename, sizeof filename, "/dev/tty%d", ttyNumber);
	if (ret < 0 || (size_t)ret >= sizeof filename) {
		fprintf(stderr, "invalid tty number or path truncated\n");
		return -1;
	}

	int fd = open(filename, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror("open tty");
		return -1;
	}

	return fd;
}
