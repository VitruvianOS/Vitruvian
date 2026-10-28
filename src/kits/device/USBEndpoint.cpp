/*
 * BUSBEndpoint: USB kit endpoint side, libusb-1.0 backend.
 *
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Bulk and interrupt run synchronously through libusb; isochronous has
 * no packet scheduling here and reports B_NOT_SUPPORTED.
 */

#include <USBKit.h>

#include <string.h>

#include <new>

#include "USBPriv.h"


BUSBEndpoint::BUSBEndpoint(BUSBInterface *interface, uint32 index,
	int rawFD)
	:
	fInterface(interface),
	fIndex(index),
	fRawFD(rawFD),
	fLibusb(NULL)
{
	memset(&fDescriptor, 0, sizeof(fDescriptor));
	memset(fReserved, 0, sizeof(fReserved));
}


BUSBEndpoint::~BUSBEndpoint()
{
}


uint32
BUSBEndpoint::Index() const
{
	return fIndex;
}


const BUSBInterface *
BUSBEndpoint::Interface() const
{
	return fInterface;
}


const BUSBConfiguration *
BUSBEndpoint::Configuration() const
{
	return fInterface->Configuration();
}


const BUSBDevice *
BUSBEndpoint::Device() const
{
	return fInterface->Device();
}


bool
BUSBEndpoint::IsBulk() const
{
	return (fDescriptor.attributes & USB_ENDPOINT_ATTR_MASK)
		== USB_ENDPOINT_ATTR_BULK;
}


bool
BUSBEndpoint::IsInterrupt() const
{
	return (fDescriptor.attributes & USB_ENDPOINT_ATTR_MASK)
		== USB_ENDPOINT_ATTR_INTERRUPT;
}


bool
BUSBEndpoint::IsIsochronous() const
{
	return (fDescriptor.attributes & USB_ENDPOINT_ATTR_MASK)
		== USB_ENDPOINT_ATTR_ISOCHRONOUS;
}


bool
BUSBEndpoint::IsControl() const
{
	return (fDescriptor.attributes & USB_ENDPOINT_ATTR_MASK)
		== USB_ENDPOINT_ATTR_CONTROL;
}


bool
BUSBEndpoint::IsInput() const
{
	return (fDescriptor.endpoint_address & USB_ENDPOINT_ADDR_DIR_IN)
		== USB_ENDPOINT_ADDR_DIR_IN;
}


bool
BUSBEndpoint::IsOutput() const
{
	return (fDescriptor.endpoint_address & USB_ENDPOINT_ADDR_DIR_IN)
		== USB_ENDPOINT_ADDR_DIR_OUT;
}


uint16
BUSBEndpoint::MaxPacketSize() const
{
	return fDescriptor.max_packet_size;
}


uint8
BUSBEndpoint::Interval() const
{
	return fDescriptor.interval;
}


const usb_endpoint_descriptor *
BUSBEndpoint::Descriptor() const
{
	return &fDescriptor;
}


ssize_t
BUSBEndpoint::ControlTransfer(uint8 requestType, uint8 request, uint16 value,
	uint16 index, uint16 length, void *data) const
{
	// Control transfers always ride the default pipe, so the device sees
	// the same request the endpoint object would have sent itself.
	return Device()->ControlTransfer(requestType, request, value, index,
		length, data);
}


ssize_t
BUSBEndpoint::InterruptTransfer(void *data, size_t length) const
{
	if (length > 0 && data == NULL)
		return B_BAD_VALUE;

	_usb_lib_state *state = (_usb_lib_state *)fLibusb;
	if (state == NULL || state->handle == NULL)
		return usb_no_handle_status(state);

	int transferred = 0;
	int result = libusb_interrupt_transfer(state->handle,
		fDescriptor.endpoint_address, (unsigned char *)data, length,
		&transferred, USB_KIT_TIMEOUT_MS);
	if (result < 0)
		return usb_lib_error(result);

	return transferred;
}


ssize_t
BUSBEndpoint::BulkTransfer(void *data, size_t length) const
{
	if (length > 0 && data == NULL)
		return B_BAD_VALUE;

	_usb_lib_state *state = (_usb_lib_state *)fLibusb;
	if (state == NULL || state->handle == NULL)
		return usb_no_handle_status(state);

	int transferred = 0;
	int result = libusb_bulk_transfer(state->handle,
		fDescriptor.endpoint_address, (unsigned char *)data, length,
		&transferred, USB_KIT_TIMEOUT_MS);
	if (result < 0)
		return usb_lib_error(result);

	return transferred;
}


ssize_t
BUSBEndpoint::IsochronousTransfer(void *data, size_t length,
	usb_iso_packet_descriptor * /*packetDescriptors*/,
	uint32 /*packetCount*/) const
{
	if (length > 0 && data == NULL)
		return B_BAD_VALUE;

	return B_NOT_SUPPORTED;
}


bool
BUSBEndpoint::IsStalled() const
{
	uint16 status = 0;
	ssize_t result = Device()->ControlTransfer(USB_REQTYPE_ENDPOINT_IN,
		USB_REQUEST_GET_STATUS, USB_FEATURE_ENDPOINT_HALT,
		fDescriptor.endpoint_address, sizeof(status), &status);
	if (result < 0)
		return false;

	return status != 0;
}


status_t
BUSBEndpoint::ClearStall() const
{
	ssize_t result = Device()->ControlTransfer(USB_REQTYPE_ENDPOINT_OUT,
		USB_REQUEST_CLEAR_FEATURE, USB_FEATURE_ENDPOINT_HALT,
		fDescriptor.endpoint_address, 0, NULL);
	if (result < 0)
		return (status_t)result;

	return B_OK;
}
