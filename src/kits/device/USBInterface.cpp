/*
 * BUSBInterface: USB kit interface side, libusb-1.0 backend.
 *
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Descriptors come from the configuration model. SetAlternate() sends
 * SET_INTERFACE and then refreshes the cache.
 */

#include <USBKit.h>

#include <string.h>

#include <new>

#include "USBPriv.h"


BUSBInterface::BUSBInterface(BUSBConfiguration *config, uint32 index,
	uint32 alternate, int rawFD)
	:
	fConfiguration(config),
	fIndex(index),
	fAlternate(alternate),
	fRawFD(rawFD),
	fEndpoints(NULL),
	fAlternateCount(0),
	fAlternates(NULL),
	fInterfaceString(NULL),
	fLibusb(NULL)
{
	memset(&fDescriptor, 0, sizeof(fDescriptor));
	memset(fReserved, 0, sizeof(fReserved));
}


BUSBInterface::~BUSBInterface()
{
	delete[] fInterfaceString;

	if (fEndpoints != NULL) {
		for (uint32 i = 0; i < fDescriptor.num_endpoints; i++)
			delete fEndpoints[i];
		delete[] fEndpoints;
	}

	if (fAlternates != NULL) {
		for (uint32 i = 0; i < fAlternateCount; i++)
			delete fAlternates[i];
		delete[] fAlternates;
	}
}


uint32
BUSBInterface::Index() const
{
	return fIndex;
}


uint32
BUSBInterface::AlternateIndex() const
{
	return fAlternate;
}


const BUSBConfiguration *
BUSBInterface::Configuration() const
{
	return fConfiguration;
}


const BUSBDevice *
BUSBInterface::Device() const
{
	return fConfiguration->Device();
}


uint8
BUSBInterface::Class() const
{
	return fDescriptor.interface_class;
}


uint8
BUSBInterface::Subclass() const
{
	return fDescriptor.interface_subclass;
}


uint8
BUSBInterface::Protocol() const
{
	return fDescriptor.interface_protocol;
}


const char *
BUSBInterface::InterfaceString() const
{
	if (fDescriptor.interface == 0)
		return "";

	if (fInterfaceString)
		return fInterfaceString;

	fInterfaceString = Device()->DecodeStringDescriptor(fDescriptor.interface);
	if (fInterfaceString == NULL)
		return "";

	return fInterfaceString;
}


const usb_interface_descriptor *
BUSBInterface::Descriptor() const
{
	return &fDescriptor;
}


status_t
BUSBInterface::OtherDescriptorAt(uint32 index, usb_descriptor *descriptor,
	size_t length) const
{
	if (length == 0 || descriptor == NULL)
		return B_BAD_VALUE;

	_usb_config_model *model = (_usb_config_model *)fConfiguration->fModel;
	if (model == NULL || fIndex >= model->interfaceCount)
		return B_ERROR;

	_usb_interface_entry *entry = &model->interfaces[fIndex];
	if (fAlternate >= entry->alternateCount)
		return B_ERROR;

	_usb_alt_entry *alt = &entry->alternates[fAlternate];

	// Class specific and vendor specific bytes in descriptor order: first
	// the ones glued to the interface descriptor, then each endpoint's.
	uint32 wanted = index;

	const uint8 *blocks[1 + 256];
	uint32 blockLengths[1 + 256];
	uint32 blockCount = 0;

	blocks[blockCount] = alt->extra;
	blockLengths[blockCount] = alt->extraLength;
	blockCount++;

	for (uint32 i = 0; i < alt->endpointCount && blockCount < 1 + 256;
			i++) {
		blocks[blockCount] = alt->endpoints[i].extra;
		blockLengths[blockCount] = alt->endpoints[i].extraLength;
		blockCount++;
	}

	for (uint32 i = 0; i < blockCount; i++) {
		uint32 offset = 0;
		while (offset < blockLengths[i]) {
			uint8 descriptorLength = blocks[i][offset];
			if (descriptorLength < 2 || offset + descriptorLength
					> blockLengths[i])
				return B_ERROR;

			if (wanted == 0) {
				if (descriptorLength > length)
					return B_BAD_VALUE;

				memcpy(descriptor, blocks[i] + offset, descriptorLength);
				return B_OK;
			}

			wanted--;
			offset += descriptorLength;
		}
	}

	// Exhausted: the legacy kit reports B_ERROR, which ends caller loops
	// written as "while OtherDescriptorAt(...) == B_OK".
	return B_ERROR;
}


uint32
BUSBInterface::CountEndpoints() const
{
	return fDescriptor.num_endpoints;
}


const BUSBEndpoint *
BUSBInterface::EndpointAt(uint32 index) const
{
	if (fEndpoints == NULL || index >= fDescriptor.num_endpoints)
		return NULL;

	return fEndpoints[index];
}


uint32
BUSBInterface::CountAlternates() const
{
	_usb_config_model *model = (_usb_config_model *)fConfiguration->fModel;
	if (model == NULL || fIndex >= model->interfaceCount)
		return 1;

	uint32 count = model->interfaces[fIndex].alternateCount;
	return count > 0 ? count : 1;
}


uint32
BUSBInterface::ActiveAlternateIndex() const
{
	const BUSBInterface *primary = fConfiguration->InterfaceAt(fIndex);
	if (primary != NULL && primary != this)
		return primary->fAlternate;

	return fAlternate;
}


const BUSBInterface *
BUSBInterface::AlternateAt(uint32 alternateIndex) const
{
	if (fAlternateCount > 0 && fAlternates != NULL) {
		if (alternateIndex >= fAlternateCount)
			return NULL;

		return fAlternates[alternateIndex];
	}

	if (fAlternateCount == 0)
		fAlternateCount = CountAlternates();
	if (alternateIndex >= fAlternateCount)
		return NULL;

	fAlternates = new(std::nothrow) BUSBInterface *[fAlternateCount];
	if (fAlternates == NULL)
		return NULL;

	for (uint32 i = 0; i < fAlternateCount; i++) {
		fAlternates[i] = new(std::nothrow) BUSBInterface(fConfiguration,
			fIndex, i, fRawFD);
		if (fAlternates[i] == NULL)
			continue;

		fAlternates[i]->fLibusb = fLibusb;
		fAlternates[i]->_UpdateDescriptorAndEndpoints();
	}

	return fAlternates[alternateIndex];
}


status_t
BUSBInterface::SetAlternate(uint32 alternateIndex)
{
	if (alternateIndex >= CountAlternates())
		return B_BAD_VALUE;

	_usb_lib_state *state = (_usb_lib_state *)fLibusb;
	if (state == NULL || state->handle == NULL)
		return usb_no_handle_status(state);

	// wValue carries the bAlternateSetting value, not our array index.
	uint16 alternateSetting = alternateIndex;
	uint16 interfaceNumber = fDescriptor.interface_number;
	_usb_config_model *model = (_usb_config_model *)fConfiguration->fModel;
	if (model != NULL && fIndex < model->interfaceCount
		&& alternateIndex < model->interfaces[fIndex].alternateCount) {
		_usb_interface_entry *entry = &model->interfaces[fIndex];
		alternateSetting = entry->alternates[alternateIndex]
			.descriptor.alternate_setting;
		interfaceNumber = entry->interfaceNumber;
	}

	int result = libusb_control_transfer(state->handle,
		USB_REQTYPE_INTERFACE_OUT, USB_REQUEST_SET_INTERFACE, alternateSetting,
		interfaceNumber, NULL, 0, USB_KIT_TIMEOUT_MS);
	if (result < 0)
		return usb_lib_error(result);

	fAlternate = alternateIndex;
	_UpdateDescriptorAndEndpoints();
	return B_OK;
}


void
BUSBInterface::_UpdateDescriptorAndEndpoints()
{
	// Every endpoint handed out before the switch dies here, as the header
	// promises, even when the new alternate equals the old one.
	if (fEndpoints != NULL) {
		for (uint32 i = 0; i < fDescriptor.num_endpoints; i++)
			delete fEndpoints[i];
		delete[] fEndpoints;
		fEndpoints = NULL;
	}

	memset(&fDescriptor, 0, sizeof(fDescriptor));

	_usb_config_model *model = (_usb_config_model *)fConfiguration->fModel;
	if (model == NULL || fIndex >= model->interfaceCount)
		return;

	_usb_interface_entry *entry = &model->interfaces[fIndex];
	if (fAlternate >= entry->alternateCount)
		return;

	_usb_alt_entry *alt = &entry->alternates[fAlternate];
	fDescriptor = alt->descriptor;

	fEndpoints = new(std::nothrow) BUSBEndpoint *[fDescriptor.num_endpoints];
	if (fEndpoints == NULL) {
		fDescriptor.num_endpoints = 0;
		return;
	}

	for (uint32 i = 0; i < fDescriptor.num_endpoints; i++) {
		BUSBEndpoint *endpoint = new(std::nothrow) BUSBEndpoint(this, i,
			fRawFD);
		if (endpoint == NULL) {
			fEndpoints[i] = NULL;
			continue;
		}

		endpoint->fLibusb = fLibusb;
		endpoint->fDescriptor = alt->endpoints[i].descriptor;
		fEndpoints[i] = endpoint;
	}
}
