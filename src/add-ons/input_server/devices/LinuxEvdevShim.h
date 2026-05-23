/*
 * LinuxEvdevShim.h — standalone evdev definitions for the Vitruvian sysroot.
 *
 * Include this INSTEAD OF <linux/input.h> and <libevdev/libevdev.h>.
 */
#ifndef _LINUX_EVDEV_SHIM_H
#define _LINUX_EVDEV_SHIM_H

/* ---- event types ---- */
#ifndef EV_SYN
#define EV_SYN		0x00
#define EV_KEY		0x01
#define EV_REL		0x02
#define EV_ABS		0x03
#define EV_MSC		0x04
#define EV_LED		0x11
#define EV_MAX		0x1f
#endif

/* ---- sync ---- */
#ifndef SYN_REPORT
#define SYN_REPORT	0
#endif

/* ---- absolute axes ---- */
#ifndef ABS_X
#define ABS_X		0x00
#define ABS_Y		0x01
#endif

/* ---- relative axes ---- */
#ifndef REL_X
#define REL_X		0x00
#define REL_Y		0x01
#define REL_HWHEEL	0x06
#define REL_WHEEL	0x08
#endif

/* ---- mouse buttons ---- */
#ifndef BTN_LEFT
#define BTN_LEFT	0x110
#define BTN_RIGHT	0x111
#define BTN_MIDDLE	0x112
#define BTN_SIDE	0x113
#define BTN_EXTRA	0x114
#endif

/* ---- all evdev key/button/axis constants ---- */
#include <linux/input-event-codes.h>

/* ---- tablet/stylus buttons ---- */
#ifndef BTN_STYLUS
#define BTN_STYLUS	0x14b
#endif
#ifndef BTN_STYLUS2
#define BTN_STYLUS2	0x14c
#endif
#ifndef BTN_TOOL_PEN
#define BTN_TOOL_PEN	0x140
#endif
#ifndef BTN_TOUCH
#define BTN_TOUCH	0x14a
#endif

/* ---- LEDs ---- */
#ifndef LED_NUML
#define LED_NUML	0x00
#define LED_CAPSL	0x01
#define LED_SCROLLL	0x02
#define LED_MAX		0x0f
#endif

/* ---- struct input_event (x86-64 kernel ABI) ---- */
#ifndef _VITRUVIAN_INPUT_EVENT_DEFINED
#define _VITRUVIAN_INPUT_EVENT_DEFINED
struct input_event {
	struct {
		long tv_sec;
		long tv_usec;
	} time;
	unsigned short	type;
	unsigned short	code;
	int		value;
};
#endif

/* Block linux/input.h from being re-included by libevdev */
#ifndef _INPUT_H
#define _INPUT_H
#endif

/* ---- ioctl encoding ---- */
#ifndef _IOC
#define _IOC(dir,type,nr,size) \
	(((unsigned int)(dir)  << 30) | \
	 ((unsigned int)(type) <<  8) | \
	 ((unsigned int)(nr)   <<  0) | \
	 ((unsigned int)(size) << 16))
#define _IOC_WRITE	1U
#define _IOC_READ	2U
#define _IOW(type,nr,sz)	_IOC(_IOC_WRITE, (type), (nr), sizeof(sz))
#define _IOR(type,nr,sz)	_IOC(_IOC_READ,  (type), (nr), sizeof(sz))
#endif

/* ---- input_id struct (used by EVIOCGID) ---- */
#ifndef _VITRUVIAN_INPUT_ID_DEFINED
#define _VITRUVIAN_INPUT_ID_DEFINED
struct input_id {
	unsigned short bustype;
	unsigned short vendor;
	unsigned short product;
	unsigned short version;
};
#endif

/* ---- evdev ioctls ---- */
#ifndef EVIOCGRAB
#define EVIOCGRAB		_IOW('E', 0x90, int)
#endif
#ifndef EVIOCGID
#define EVIOCGID		_IOR('E', 0x02, struct input_id)
#endif
#ifndef EVIOCSREP
#define EVIOCSREP		_IOW('E', 0x03, unsigned int[2])
#endif
#ifndef EVIOCSLED
#define EVIOCSLED(len)		_IOC(_IOC_WRITE, 'E', 0x12, (len) * sizeof(int))
#endif
#ifndef EVIOCGLED
#define EVIOCGLED(len)		_IOC(_IOC_READ,  'E', 0x19, len)
#endif

#include <libevdev/libevdev.h>

#endif /* _LINUX_EVDEV_SHIM_H */
