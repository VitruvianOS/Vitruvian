/*
 * Copyright 2019, Dario Casalinuovo.
 * Distributed under the terms of the GPL License.
 */
#ifndef _TTY_H
#define _TTY_H

#include <linux/vt.h>
#include <linux/kd.h>
#include <signal.h>
#include <termios.h>


class TTy {
public:
			static status_t		InitTTy(int ttyNumber, void(*callback)(int sig));
			static int			OpenTTy(int ttyNumber);
			static void			DeinitTTy();
			static int gTTy;

private:
			static struct sigaction	fSigUsr1;
			static struct sigaction	fSigUsr2;

			static struct vt_mode		fVtMode;
			static struct termios		fTermios;
};

#endif
