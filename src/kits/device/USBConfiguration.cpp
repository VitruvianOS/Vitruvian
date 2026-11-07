/*
 * BUSBConfiguration: USB kit configuration side, libusb-1.0 backend.
 *
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * _Load() copies the configuration descriptor tree out of libusb (which
 * frees its own copy) into the model the interfaces and endpoints walk.
 */

#include <USBKit.h>

#include <stdlib.h>
#include <string.h>

#include <new>

#include "USBPriv.h"


static void
copy_extra(uint8 **target, uint32 *targetLength, const unsigned char *source,
	int sourceLength)
{
	*target = NULL;
	*targetLength = 0;

	if (source == NULL || sourceLength <= 0)
		return;

	*target = (uint8 *)malloc(sourceLength);
	if (*target == NULL)
		return;

	memcpy(*target, source, sourceLength);
	*targetLength = sourceLength;
}


static void
free_model(_usb_config_model *model)
{
	if (model == NULL)
		return;

	for (uint32 i = 0; i < model->interfaceCount; i++) {
		_usb_interface_entry *entry = &model->interfaces[i];
		for (uint32 j = 0; j < entry->alternateCount; j++) {
			_usb_alt_entry *alt = &entry->alternates[j];
			for (uint32 k = 0; k < alt->endpointCount; k++)
				free(alt->endpoints[k].extra);
			delete[] alt->endpoints;
			free(alt->extra);
		}
		delete[] entry->alternates;
	}

	delete[] model->interfaces;
	delete model;
}


BUSBConfiguration::BUSBConfiguration(BUSBDevice *device, uint32 index,
	int rawFD)
	:
	fDevice(device),
	fIndex(index),
	fRawFD(rawFD),
	fInterfaces(NULL),
	fConfigurationString(NULL),
	fModel(NULL),
	fLibusb(NULL)
{
	memset(&fDescriptor, 0, sizeof(fDescriptor));
	memset(fReserved, 0, sizeof(fReserved));
}


BUSBConfiguration::~BUSBConfiguration()
{
	delete[] fConfigurationString;

	if (fInterfaces != NULL) {
		for (uint32 i = 0; i < fDescriptor.number_interfaces; i++)
			delete fInterfaces[i];
		delete[] fInterfaces;
	}

	free_model((_usb_config_model *)fModel);
	fModel = NULL;
}


void
BUSBConfiguration::_Load()
{
	_usb_lib_state *state = (_usb_lib_state *)fLibusb;
	if (state == NULL || state->device == NULL)
		return;

	struct libusb_config_descriptor *config = NULL;
	if (libusb_get_config_descriptor(state->device, fIndex, &config) != 0)
		return;

	fDescriptor.length = config->bLength;
	fDescriptor.descriptor_type = config->bDescriptorType;
	fDescriptor.total_length = config->wTotalLength;
	fDescriptor.number_interfaces = config->bNumInterfaces;
	fDescriptor.configuration_value = config->bConfigurationValue;
	fDescriptor.configuration = config->iConfiguration;
	fDescriptor.attributes = config->bmAttributes;
	fDescriptor.max_power = config->MaxPower;

	_usb_config_model *model = new(std::nothrow) _usb_config_model;
	if (model == NULL) {
		libusb_free_config_descriptor(config);
		return;
	}

	model->interfaceCount = config->bNumInterfaces;
	model->interfaces = new(std::nothrow) _usb_interface_entry[
		config->bNumInterfaces];
	if (model->interfaces == NULL) {
		delete model;
		libusb_free_config_descriptor(config);
		return;
	}
	memset(model->interfaces, 0,
		sizeof(_usb_interface_entry) * config->bNumInterfaces);

	for (uint32 i = 0; i < config->bNumInterfaces; i++) {
		const struct libusb_interface *interface = &config->interface[i];
		_usb_interface_entry *entry = &model->interfaces[i];
		entry->interfaceNumber = 0;
		entry->alternateCount = interface->num_altsetting;
		entry->alternates = new(std::nothrow) _usb_alt_entry[
			interface->num_altsetting];
		if (entry->alternates == NULL) {
			entry->alternateCount = 0;
			continue;
		}
		memset(entry->alternates, 0, sizeof(_usb_alt_entry)
			* interface->num_altsetting);

		for (int32 j = 0; j < interface->num_altsetting; j++) {
			const struct libusb_interface_descriptor *alt
				= &interface->altsetting[j];
			_usb_alt_entry *altEntry = &entry->alternates[j];

			if (j == 0)
				entry->interfaceNumber = alt->bInterfaceNumber;

			altEntry->descriptor.length = alt->bLength;
			altEntry->descriptor.descriptor_type = alt->bDescriptorType;
			altEntry->descriptor.interface_number = alt->bInterfaceNumber;
			altEntry->descriptor.alternate_setting = alt->bAlternateSetting;
			altEntry->descriptor.num_endpoints = alt->bNumEndpoints;
			altEntry->descriptor.interface_class = alt->bInterfaceClass;
			altEntry->descriptor.interface_subclass = alt->bInterfaceSubClass;
			altEntry->descriptor.interface_protocol = alt->bInterfaceProtocol;
			altEntry->descriptor.interface = alt->iInterface;
			copy_extra(&altEntry->extra, &altEntry->extraLength, alt->extra,
				alt->extra_length);

			altEntry->endpointCount = alt->bNumEndpoints;
			altEntry->endpoints = new(std::nothrow) _usb_endpoint_entry[
				alt->bNumEndpoints];
			if (altEntry->endpoints == NULL) {
				altEntry->endpointCount = 0;
				continue;
			}
			memset(altEntry->endpoints, 0, sizeof(_usb_endpoint_entry)
				* alt->bNumEndpoints);

			for (uint8 k = 0; k < alt->bNumEndpoints; k++) {
				const struct libusb_endpoint_descriptor *endpoint
					= &alt->endpoint[k];
				_usb_endpoint_entry *endpointEntry
					= &altEntry->endpoints[k];

				endpointEntry->descriptor.length = endpoint->bLength;
				endpointEntry->descriptor.descriptor_type
					= endpoint->bDescriptorType;
				endpointEntry->descriptor.endpoint_address
					= endpoint->bEndpointAddress;
				endpointEntry->descriptor.attributes = endpoint->bmAttributes;
				endpointEntry->descriptor.max_packet_size
					= endpoint->wMaxPacketSize;
				endpointEntry->descriptor.interval = endpoint->bInterval;
				copy_extra(&endpointEntry->extra,
					&endpointEntry->extraLength, endpoint->extra,
					endpoint->extra_length);
			}
		}
	}

	fModel = model;
	libusb_free_config_descriptor(config);

	fInterfaces = new(std::nothrow) BUSBInterface *[
		fDescriptor.number_interfaces];
	if (fInterfaces == NULL)
		return;

	for (uint32 i = 0; i < fDescriptor.number_interfaces; i++) {
		fInterfaces[i] = new(std::nothrow) BUSBInterface(this, i, 0, fRawFD);
		if (fInterfaces[i] == NULL)
			continue;

		fInterfaces[i]->fLibusb = fLibusb;
		fInterfaces[i]->_UpdateDescriptorAndEndpoints();
	}
}


uint32
BUSBConfiguration::Index() const
{
	return fIndex;
}


const BUSBDevice *
BUSBConfiguration::Device() const
{
	return fDevice;
}


const char *
BUSBConfiguration::ConfigurationString() const
{
	if (fDescriptor.configuration == 0)
		return "";

	if (fConfigurationString)
		return fConfigurationString;

	fConfigurationString = Device()->DecodeStringDescriptor(
		fDescriptor.configuration);
	if (fConfigurationString == NULL)
		return "";

	return fConfigurationString;
}


const usb_configuration_descriptor *
BUSBConfiguration::Descriptor() const
{
	return &fDescriptor;
}


uint32
BUSBConfiguration::CountInterfaces() const
{
	return fDescriptor.number_interfaces;
}


const BUSBInterface *
BUSBConfiguration::InterfaceAt(uint32 index) const
{
	if (fInterfaces == NULL || index >= fDescriptor.number_interfaces)
		return NULL;

	return fInterfaces[index];
}
