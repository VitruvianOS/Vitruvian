/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "RumbleTester.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>

#include "LinuxEvdevShim.h"


#ifndef EV_FF
#define EV_FF					0x15
#endif
#ifndef FF_RUMBLE
#define FF_RUMBLE				0x50
#endif
#ifndef FF_MAX
#define FF_MAX					0x7f
#endif


// kernel UAPI layout (linux/input.h); the shim carries no FF definitions
struct _ff_trigger {
	uint16		button;
	uint16		interval;
};

struct _ff_replay {
	uint16		length;
	uint16		delay;
};

struct _ff_rumble_effect {
	uint16		strong_magnitude;
	uint16		weak_magnitude;
};

struct _ff_effect {
	uint16		type;
	int16		id;
	uint16		direction;
	_ff_trigger	trigger;
	_ff_replay	replay;
	uint8		_padding[16 - 14];
	union {
		uint8			_size[32];
		_ff_rumble_effect rumble;
	} u;
};

#ifndef EVIOCSFF
#define EVIOCSFF		_IOW('E', 0x80, struct _ff_effect)
#endif
#ifndef EVIOCRMFF
#define EVIOCRMFF		_IOW('E', 0x81, int)
#endif


static bool
test_bit(const unsigned char* bits, int32 code)
{
	return (bits[code / 8] & (1 << (code % 8))) != 0;
}


RumbleTester::RumbleTester()
	:
	fFD(-1),
	fEffectID(-1)
{
}


RumbleTester::~RumbleTester()
{
	UnsetDevice();
}


status_t
RumbleTester::SetDevice(const char* devicePath)
{
	UnsetDevice();

	// write access is required to upload and play effects
	fFD = open(devicePath, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (fFD < 0)
		return B_ERROR;

	unsigned char ffBits[(FF_MAX + 7) / 8];
	memset(ffBits, 0, sizeof(ffBits));
	if (ioctl(fFD, EVIOCGBIT(EV_FF, sizeof(ffBits)), ffBits) < 0
		|| !test_bit(ffBits, FF_RUMBLE)) {
		UnsetDevice();
		return B_NOT_SUPPORTED;
	}

	struct _ff_effect effect;
	memset(&effect, 0, sizeof(effect));
	effect.type = FF_RUMBLE;
	effect.id = -1;
	effect.replay.length = 500;
	if (ioctl(fFD, EVIOCSFF, &effect) < 0) {
		UnsetDevice();
		return B_ERROR;
	}

	fEffectID = effect.id;
	return B_OK;
}


void
RumbleTester::UnsetDevice()
{
	if (fFD >= 0) {
		if (fEffectID >= 0)
			ioctl(fFD, EVIOCRMFF, &fEffectID);
		close(fFD);
	}
	fFD = -1;
	fEffectID = -1;
}


bool
RumbleTester::IsSupported() const
{
	return fEffectID >= 0;
}


status_t
RumbleTester::Trigger(uint32 durationMs, uint16 strong, uint16 weak)
{
	if (!IsSupported())
		return B_NOT_SUPPORTED;

	struct _ff_effect effect;
	memset(&effect, 0, sizeof(effect));
	effect.type = FF_RUMBLE;
	effect.id = fEffectID;
	effect.replay.length = durationMs;
	effect.u.rumble.strong_magnitude = strong;
	effect.u.rumble.weak_magnitude = weak;
	if (ioctl(fFD, EVIOCSFF, &effect) < 0)
		return B_ERROR;

	struct input_event event;
	memset(&event, 0, sizeof(event));
	event.type = EV_FF;
	event.code = fEffectID;
	event.value = 1;
	if (write(fFD, &event, sizeof(event)) != (ssize_t)sizeof(event))
		return B_ERROR;
	return B_OK;
}
