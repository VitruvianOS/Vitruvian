/*
 * BUSBDevice: USB kit device side, libusb-1.0 backend.
 *
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Descriptors are read without opening the device; a handle the user may
 * not get answers every transfer with B_PERMISSION_DENIED, never root.
 */

#include <USBKit.h>

#include <ByteOrder.h>
#include <String.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <new>

#include <Locker.h>

#include "USBPriv.h"


static BLocker sContextLock;
static libusb_context *sContext = NULL;


static libusb_context *
usb_context()
{
	if (!sContextLock.Lock())
		return NULL;

	if (sContext == NULL && libusb_init(&sContext) != 0)
		sContext = NULL;

	sContextLock.Unlock();
	return sContext;
}


BUSBDevice::BUSBDevice(const char *path)
	:
	fPath(NULL),
	fRawFD(-1),
	fConfigurations(NULL),
	fActiveConfiguration(0),
	fManufacturerString(NULL),
	fProductString(NULL),
	fSerialNumberString(NULL),
	fLibusb(NULL),
	fInitStatus(B_NO_INIT)
{
	memset(&fDescriptor, 0, sizeof(fDescriptor));

	if (path != NULL)
		SetTo(path);
}


BUSBDevice::~BUSBDevice()
{
	Unset();
}


status_t
BUSBDevice::InitCheck()
{
	return fInitStatus;
}


status_t
BUSBDevice::SetTo(const char *path)
{
	Unset();

	if (path == NULL)
		return B_BAD_VALUE;

	int busNumber = -1;
	int deviceNumber = -1;
	char extra;
	if (sscanf(path, "/dev/bus/usb/%3d/%3d%c", &busNumber, &deviceNumber,
			&extra) != 2 || busNumber < 0 || deviceNumber < 0)
		return B_BAD_VALUE;

	fPath = strdup(path);
	if (fPath == NULL) {
		fInitStatus = B_NO_MEMORY;
		return fInitStatus;
	}

	libusb_context *context = usb_context();
	if (context == NULL) {
		Unset();
		fInitStatus = B_ERROR;
		return fInitStatus;
	}

	libusb_device **list = NULL;
	ssize_t count = libusb_get_device_list(context, &list);
	if (count < 0) {
		Unset();
		fInitStatus = B_ERROR;
		return fInitStatus;
	}

	libusb_device *found = NULL;
	for (ssize_t i = 0; i < count; i++) {
		if (libusb_get_bus_number(list[i]) == busNumber
			&& libusb_get_device_address(list[i]) == deviceNumber) {
			found = libusb_ref_device(list[i]);
			break;
		}
	}
	libusb_free_device_list(list, 1);

	if (found == NULL) {
		Unset();
		fInitStatus = B_ENTRY_NOT_FOUND;
		return fInitStatus;
	}

	struct libusb_device_descriptor descriptor;
	if (libusb_get_device_descriptor(found, &descriptor) != 0) {
		libusb_unref_device(found);
		Unset();
		fInitStatus = B_ERROR;
		return fInitStatus;
	}

	fDescriptor.length = descriptor.bLength;
	fDescriptor.descriptor_type = descriptor.bDescriptorType;
	fDescriptor.usb_version = descriptor.bcdUSB;
	fDescriptor.device_class = descriptor.bDeviceClass;
	fDescriptor.device_subclass = descriptor.bDeviceSubClass;
	fDescriptor.device_protocol = descriptor.bDeviceProtocol;
	fDescriptor.max_packet_size_0 = descriptor.bMaxPacketSize0;
	fDescriptor.vendor_id = descriptor.idVendor;
	fDescriptor.product_id = descriptor.idProduct;
	fDescriptor.device_version = descriptor.bcdDevice;
	fDescriptor.manufacturer = descriptor.iManufacturer;
	fDescriptor.product = descriptor.iProduct;
	fDescriptor.serial_number = descriptor.iSerialNumber;
	fDescriptor.num_configurations = descriptor.bNumConfigurations;

	_usb_lib_state *state = new(std::nothrow) _usb_lib_state;
	if (state == NULL) {
		libusb_unref_device(found);
		Unset();
		fInitStatus = B_NO_MEMORY;
		return fInitStatus;
	}

	state->device = found;
	state->handle = NULL;
	state->openStatus = B_ERROR;

	libusb_device_handle *handle = NULL;
	int openResult = libusb_open(found, &handle);
	if (openResult == 0) {
		state->handle = handle;
		state->openStatus = B_OK;
	} else
		state->openStatus = usb_lib_error(openResult);

	fLibusb = state;

	// The configuration value is not necessarily the index; map it.
	fActiveConfiguration = 0;
	int configurationValue = 0;
	_usb_lib_state *loaded = (_usb_lib_state *)fLibusb;
	if (loaded->handle != NULL
		&& libusb_get_configuration(loaded->handle, &configurationValue) == 0
		&& configurationValue > 0) {
		for (uint32 i = 0; i < fDescriptor.num_configurations; i++) {
			struct libusb_config_descriptor *config = NULL;
			if (libusb_get_config_descriptor(found, i, &config) != 0)
				continue;

			uint8 value = config->bConfigurationValue;
			libusb_free_config_descriptor(config);

			if (value == configurationValue) {
				fActiveConfiguration = i;
				break;
			}
		}
	}

	fConfigurations = new(std::nothrow) BUSBConfiguration *[
		fDescriptor.num_configurations];
	if (fConfigurations == NULL) {
		Unset();
		fInitStatus = B_NO_MEMORY;
		return fInitStatus;
	}

	for (uint32 i = 0; i < fDescriptor.num_configurations; i++) {
		fConfigurations[i] = new(std::nothrow) BUSBConfiguration(this, i,
			fRawFD);
		if (fConfigurations[i] == NULL)
			continue;

		fConfigurations[i]->fLibusb = fLibusb;
		fConfigurations[i]->_Load();
	}

	fInitStatus = B_OK;
	return B_OK;
}


void
BUSBDevice::Unset()
{
	if (fConfigurations != NULL) {
		for (uint32 i = 0; i < fDescriptor.num_configurations; i++)
			delete fConfigurations[i];

		delete[] fConfigurations;
		fConfigurations = NULL;
	}

	_usb_lib_state *state = (_usb_lib_state *)fLibusb;
	if (state != NULL) {
		if (state->handle != NULL)
			libusb_close(state->handle);
		if (state->device != NULL)
			libusb_unref_device(state->device);
		delete state;
	}
	fLibusb = NULL;

	free(fPath);
	fPath = NULL;

	delete[] fManufacturerString;
	delete[] fProductString;
	delete[] fSerialNumberString;
	fManufacturerString = NULL;
	fProductString = NULL;
	fSerialNumberString = NULL;

	memset(&fDescriptor, 0, sizeof(fDescriptor));
	fActiveConfiguration = 0;
	fInitStatus = B_NO_INIT;
}


const char *
BUSBDevice::Location() const
{
	if (fPath == NULL || strlen(fPath) < 12)
		return NULL;

	return &fPath[12];
}


bool
BUSBDevice::IsHub() const
{
	return fDescriptor.device_class == 0x09;
}


uint16
BUSBDevice::USBVersion() const
{
	return fDescriptor.usb_version;
}


uint8
BUSBDevice::Class() const
{
	return fDescriptor.device_class;
}


uint8
BUSBDevice::Subclass() const
{
	return fDescriptor.device_subclass;
}


uint8
BUSBDevice::Protocol() const
{
	return fDescriptor.device_protocol;
}


uint8
BUSBDevice::MaxEndpoint0PacketSize() const
{
	return fDescriptor.max_packet_size_0;
}


uint16
BUSBDevice::VendorID() const
{
	return fDescriptor.vendor_id;
}


uint16
BUSBDevice::ProductID() const
{
	return fDescriptor.product_id;
}


uint16
BUSBDevice::Version() const
{
	return fDescriptor.device_version;
}


const char *
BUSBDevice::ManufacturerString() const
{
	if (fDescriptor.manufacturer == 0)
		return "";

	if (fManufacturerString)
		return fManufacturerString;

	fManufacturerString = DecodeStringDescriptor(fDescriptor.manufacturer);
	if (fManufacturerString == NULL)
		return "";

	return fManufacturerString;
}


const char *
BUSBDevice::ProductString() const
{
	if (fDescriptor.product == 0)
		return "";

	if (fProductString)
		return fProductString;

	fProductString = DecodeStringDescriptor(fDescriptor.product);
	if (fProductString == NULL)
		return "";

	return fProductString;
}


const char *
BUSBDevice::SerialNumberString() const
{
	if (fDescriptor.serial_number == 0)
		return "";

	if (fSerialNumberString)
		return fSerialNumberString;

	fSerialNumberString = DecodeStringDescriptor(fDescriptor.serial_number);
	if (fSerialNumberString == NULL)
		return "";

	return fSerialNumberString;
}


const usb_device_descriptor *
BUSBDevice::Descriptor() const
{
	return &fDescriptor;
}


size_t
BUSBDevice::GetStringDescriptor(uint32 index,
	usb_string_descriptor *descriptor, size_t length) const
{
	if (descriptor == NULL)
		return (size_t)B_BAD_VALUE;

	_usb_lib_state *state = (_usb_lib_state *)fLibusb;
	if (state == NULL || state->handle == NULL)
		return 0;

	uint16 value = (USB_DESCRIPTOR_STRING << 8) | index;

	// Most devices speak US English; some only answer the language list
	// request, so fall back to index 0 before giving up.
	int result = libusb_control_transfer(state->handle, USB_REQTYPE_DEVICE_IN,
		USB_REQUEST_GET_DESCRIPTOR, value, 0x0409, (unsigned char *)descriptor,
		length, USB_KIT_TIMEOUT_MS);
	if (result < 0)
		result = libusb_control_transfer(state->handle, USB_REQTYPE_DEVICE_IN,
			USB_REQUEST_GET_DESCRIPTOR, value, 0, (unsigned char *)descriptor,
			length, USB_KIT_TIMEOUT_MS);

	if (result < 0)
		return 0;

	return result;
}


char *
BUSBDevice::DecodeStringDescriptor(uint32 index) const
{
	char buffer[300];
	usb_string_descriptor *stringDescriptor = (usb_string_descriptor *)buffer;

	size_t total = GetStringDescriptor(index, stringDescriptor, sizeof(buffer));
	if (total < 4)
		return NULL;

	// The wire carries UTF-16 little endian after the two byte header.
	int32 units = ((int32)total - 2) / 2;
	const uint16 *wire = (const uint16 *)stringDescriptor->string;

	BString decoded;
	for (int32 i = 0; i < units; i++) {
		uint16 character = B_LENDIAN_TO_HOST_INT16(wire[i]);
		if (character < 0x80)
			decoded << (char)character;
		else if (character < 0x800) {
			decoded << (char)(0xc0 | (character >> 6));
			decoded << (char)(0x80 | (character & 0x3f));
		} else {
			decoded << (char)(0xe0 | (character >> 12));
			decoded << (char)(0x80 | ((character >> 6) & 0x3f));
			decoded << (char)(0x80 | (character & 0x3f));
		}
	}

	char *result = new(std::nothrow) char[decoded.Length() + 1];
	if (result == NULL)
		return NULL;

	memcpy(result, decoded.String(), decoded.Length() + 1);
	return result;
}


size_t
BUSBDevice::GetDescriptor(uint8 type, uint8 index, uint16 languageID,
	void *data, size_t length) const
{
	if (length > 0 && data == NULL)
		return (size_t)B_BAD_VALUE;

	_usb_lib_state *state = (_usb_lib_state *)fLibusb;
	if (state == NULL || state->handle == NULL)
		return 0;

	int result = libusb_control_transfer(state->handle, USB_REQTYPE_DEVICE_IN,
		USB_REQUEST_GET_DESCRIPTOR, (type << 8) | index, languageID,
		(unsigned char *)data, length, USB_KIT_TIMEOUT_MS);
	if (result < 0)
		return 0;

	return result;
}


uint32
BUSBDevice::CountConfigurations() const
{
	return fDescriptor.num_configurations;
}


const BUSBConfiguration *
BUSBDevice::ConfigurationAt(uint32 index) const
{
	if (fConfigurations == NULL || index >= fDescriptor.num_configurations)
		return NULL;

	return fConfigurations[index];
}


const BUSBConfiguration *
BUSBDevice::ActiveConfiguration() const
{
	if (fConfigurations == NULL
		|| fActiveConfiguration >= fDescriptor.num_configurations)
		return NULL;

	return fConfigurations[fActiveConfiguration];
}


status_t
BUSBDevice::SetConfiguration(const BUSBConfiguration *configuration)
{
	if (configuration == NULL
		|| configuration->Index() >= fDescriptor.num_configurations)
		return B_BAD_VALUE;

	_usb_lib_state *state = (_usb_lib_state *)fLibusb;
	if (state == NULL || state->handle == NULL)
		return usb_no_handle_status(state);

	int result = libusb_set_configuration(state->handle,
		configuration->Descriptor()->configuration_value);
	if (result < 0)
		return usb_lib_error(result);

	fActiveConfiguration = configuration->Index();
	return B_OK;
}


ssize_t
BUSBDevice::ControlTransfer(uint8 requestType, uint8 request, uint16 value,
	uint16 index, uint16 length, void *data) const
{
	if (length > 0 && data == NULL)
		return B_BAD_VALUE;

	_usb_lib_state *state = (_usb_lib_state *)fLibusb;
	if (state == NULL || state->handle == NULL)
		return usb_no_handle_status(state);

	int result = libusb_control_transfer(state->handle, requestType, request,
		value, index, (unsigned char *)data, length, USB_KIT_TIMEOUT_MS);
	if (result < 0)
		return usb_lib_error(result);

	return result;
}


void
BUSBDevice::_ReservedUSBDevice1()
{
}


void
BUSBDevice::_ReservedUSBDevice2()
{
}


void
BUSBDevice::_ReservedUSBDevice3()
{
}


void
BUSBDevice::_ReservedUSBDevice4()
{
}


void
BUSBDevice::_ReservedUSBDevice5()
{
}
