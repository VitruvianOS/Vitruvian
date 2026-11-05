/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _XVITRUVIAN_INPUT_PROTOCOL_H
#define _XVITRUVIAN_INPUT_PROTOCOL_H


// Fields the input_server device add-ons attach to seat events so the
// per-session XVitruvian X server can rebuild XI2 raw events, the XI2
// device list, XI2.2 touch and smooth-scroll valuators from the tap.
// Consumed by src/add-ons/input_server/filters/xvitruvian and by the
// DDX; keep names and semantics in lock-step across the three.


#include <SupportDefs.h>


// The filter creates the event tap under this port name; the X server
// find_port()s it and reads flattened BMessages in its own poll loop.
#define XVITRUVIAN_INPUT_PORT_NAME	"v-os:xvitruvian_input"


// Synthetic messages the filter prepends to the tap (not seat events).
enum {
	kXVDeviceAdded		= 'xVda',
		// be:device_id, be:device_name, be:device_subtype, be:device_flags
	kXVDeviceRemoved	= 'xVdr',
		// be:device_id; synthesised when the roster drops the device
	kXVHeartbeat		= 'xVhb',
		// emitted periodically; carries no payload, keeps latency probes
		// honest across an idle seat
};

// Touch events (XI2.2); seat-stream codes private to the tap until the
// Interface Kit grows public B_TOUCH_* constants.
enum {
	kXVTouchDown		= 'xVtD',
	kXVTouchMove		= 'xVtM',
	kXVTouchUp			= 'xVtU',
	kXVTouchCancel		= 'xVtC',
		// where, be:touch_id, be:pressure, be:device_*
};


// be:device_flags bits: what an XI2 device for this physical device
// must expose. Set by the device add-ons, forwarded by the filter.
enum {
	kXVDeviceHasKeyboard	= 1 << 0,
	kXVDeviceHasPointer		= 1 << 1,
	kXVDeviceHasTouch		= 1 << 2,
	kXVDeviceHasJoystick	= 1 << 3,
	kXVDeviceHasAbsAxes		= 1 << 4,
	kXVDeviceVirtual		= 1 << 5,
		// uinput-backed (Steam Input, lizard mode); the DDX mirrors the
		// kernel's ID_INPUT_*, games must see these as ordinary devices
	kXVDeviceDirectTouch	= 1 << 6,
};


// Event field names beyond the seat event's be:evdev_code / be:device_id /
// be:device_name / be:raw_delta_{x,y} / be:wheel_delta_{x,y}.
#define XV_FIELD_DEVICE_FLAGS	"be:device_flags"
#define XV_FIELD_ABS_X			"be:abs_x"
#define XV_FIELD_ABS_Y			"be:abs_y"
	// normalised 0..1 against the device axis range; XI2 valuator mask
#define XV_FIELD_TOUCH_ID		"be:touch_id"
#define XV_FIELD_PRESSURE		"be:pressure"
#define XV_FIELD_WHEEL_HI_X		"be:wheel_hi_res_x"
#define XV_FIELD_WHEEL_HI_Y		"be:wheel_hi_res_y"
	// fractional detents (120 units per detent at the evdev layer)
#define XV_FIELD_RELATIVE		"be:relative"
	// true when where is a frozen lock point and motion lives in the
	// raw/valuator deltas only (pointer-constraint relative mode)


// Valuator helpers shared by the add-ons (produce) and the checks.
static inline float
xv_axis_normalize(int32 value, int32 minimum, int32 maximum)
{
	if (maximum <= minimum)
		return 0.0f;

	float unit = (float)(value - minimum) / (float)(maximum - minimum);
	if (unit < 0.0f)
		unit = 0.0f;
	else if (unit > 1.0f)
		unit = 1.0f;
	return unit;
}


// evdev REL_*_HI_RES carries 120 units per detent; XI2 smooth-scroll
// valuators want fractional detents.
static inline float
xv_wheel_detents(int32 hiResUnits)
{
	return (float)hiResUnits / 120.0f;
}


// X keycodes are evdev codes + 8; xkb and the X server share this.
static inline uint8
xv_x_keycode(uint32 evdevCode)
{
	return (uint8)((evdevCode + 8) & 0xff);
}


#endif // _XVITRUVIAN_INPUT_PROTOCOL_H
