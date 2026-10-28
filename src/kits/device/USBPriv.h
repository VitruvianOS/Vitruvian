/*
 * Private plumbing shared by the USB kit sources.
 *
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _USB_PRIV_H
#define _USB_PRIV_H

#include <SupportDefs.h>
#include <USB_spec.h>

#include <libusb.h>


// The legacy kit left the timeout to the stack; libusb needs a number.
#define USB_KIT_TIMEOUT_MS		5000

// libusb state hung off the fLibusb member of every object in a device
// tree: SetTo opens the handle once and the parents pass it down.
struct _usb_lib_state {
	libusb_device *				device;
	libusb_device_handle *		handle;
	status_t					openStatus;
};


// Deep copy of one endpoint: libusb points into a descriptor blob it
// frees, so the kit keeps its own bytes for as long as the objects live.
struct _usb_endpoint_entry {
	usb_endpoint_descriptor	descriptor;
	uint8 *					extra;
	uint32					extraLength;
};


struct _usb_alt_entry {
	usb_interface_descriptor	descriptor;
	_usb_endpoint_entry *		endpoints;
	uint32					endpointCount;
	uint8 *					extra;
	uint32					extraLength;
};


struct _usb_interface_entry {
	uint32					interfaceNumber;
	_usb_alt_entry *			alternates;
	uint32					alternateCount;
};


struct _usb_config_model {
	_usb_interface_entry *	interfaces;
	uint32					interfaceCount;
};


// Maps a libusb transfer error onto the kit's status codes; a device the
// user cannot open reports B_PERMISSION_DENIED and nothing escalates.
inline status_t
usb_lib_error(int result)
{
	if (result == LIBUSB_ERROR_ACCESS)
		return B_PERMISSION_DENIED;
	if (result == LIBUSB_ERROR_TIMEOUT)
		return B_TIMED_OUT;
	return B_ERROR;
}


// Status for an operation that needs the handle when there is none.
inline status_t
usb_no_handle_status(const _usb_lib_state *state)
{
	if (state == NULL)
		return B_NO_INIT;
	return state->openStatus;
}


#endif // _USB_PRIV_H
