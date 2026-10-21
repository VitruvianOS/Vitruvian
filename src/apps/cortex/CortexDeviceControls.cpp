/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "CortexDeviceControls.h"

#include <new>
#include <string.h>

#include <Autolock.h>
#include <Locker.h>
#include <ObjectList.h>
#include <OS.h>
#include <String.h>

#include <media2/MediaGraph.h>
#include <media2/ParameterWeb.h>


enum {
	kDeviceVolume		= 1,
	kDeviceMute			= 2,
	kChannelVolumeBase	= 100,
	kMaxDeviceChannels	= 8,
	kDeviceProfile		= 200,
	kDevicePort			= 201
};


CortexDeviceControllable::CortexDeviceControllable(media_client_id id)
	:
	fId(id)
{
	_BuildWeb();
}


CortexDeviceControllable::~CortexDeviceControllable()
{
}


void
CortexDeviceControllable::_BuildWeb()
{
	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL)
		return;

	BParameterWeb* web = new(std::nothrow) BParameterWeb();
	if (web == NULL)
		return;

	BParameterGroup* volume = web->MakeGroup("Volume");
	if (volume == NULL) {
		delete web;
		return;
	}

	float master = 0.0f;
	bool mute = false;
	graph->GetDeviceVolume(fId, &master, &mute);
	volume->MakeContinuousParameter(kDeviceVolume, B_MEDIA_NO_TYPE,
		"Master Volume", B_MASTER_GAIN, "", 0.0f, 1.0f, 0.01f);
	BDiscreteParameter* muteParameter = volume->MakeDiscreteParameter(
		kDeviceMute, B_MEDIA_NO_TYPE, "Mute", B_MUTE);
	if (muteParameter != NULL) {
		muteParameter->AddItem(0, "Off");
		muteParameter->AddItem(1, "On");
	}

	float channelVolumes[kMaxDeviceChannels];
	size_t channelCount = 0;
	memset(channelVolumes, 0, sizeof(channelVolumes));
	graph->GetDeviceChannelVolumes(fId, channelVolumes, kMaxDeviceChannels,
		&channelCount);
	if (channelCount > kMaxDeviceChannels)
		channelCount = kMaxDeviceChannels;
	for (size_t i = 0; i < channelCount; i++) {
		BString name;
		name.SetToFormat("Channel %d", (int)i + 1);
		volume->MakeContinuousParameter((int32)(kChannelVolumeBase + i),
			B_MEDIA_NO_TYPE, name.String(), B_GENERIC, "", 0.0f, 1.0f,
			0.01f);
	}

	BParameterGroup* routing = web->MakeGroup("Routing");
	if (routing != NULL) {
		BObjectList<BMediaGraph::DeviceProfileInfo, true> profiles;
		int32 active = -1;
		if (graph->GetDeviceProfiles(fId, &profiles, &active) == B_OK
			&& profiles.CountItems() > 0) {
			BDiscreteParameter* profile = routing->MakeDiscreteParameter(
				kDeviceProfile, B_MEDIA_NO_TYPE, "Profile", B_GENERIC);
			if (profile != NULL) {
				for (int32 i = 0; i < profiles.CountItems(); i++) {
					BMediaGraph::DeviceProfileInfo* info
						= profiles.ItemAt(i);
					profile->AddItem(info->index,
						info->name.Length() > 0
							? info->name.String() : "profile");
				}
			}
		}

		BObjectList<BMediaGraph::DevicePortInfo, true> ports;
		if (graph->GetDevicePorts(fId, &ports) == B_OK
			&& ports.CountItems() > 0) {
			BDiscreteParameter* port = routing->MakeDiscreteParameter(
				kDevicePort, B_MEDIA_NO_TYPE, "Port", B_INPUT_MUX);
			if (port != NULL) {
				for (int32 i = 0; i < ports.CountItems(); i++) {
					BMediaGraph::DevicePortInfo* info = ports.ItemAt(i);
					port->AddItem(info->portId,
						info->name.Length() > 0
							? info->name.String() : "port");
				}
			}
		}
	}

	SetParameterWeb(web);

	// Seed the stored values; applying what the device already has is
	// harmless and gives the controls something to show.
	SetParameterValue(kDeviceVolume, system_time(), &master, sizeof(master));
	const int32 muteValue = mute ? 1 : 0;
	SetParameterValue(kDeviceMute, system_time(), &muteValue,
		sizeof(muteValue));
	for (size_t i = 0; i < channelCount; i++) {
		const int32 id = (int32)(kChannelVolumeBase + i);
		SetParameterValue(id, system_time(), &channelVolumes[i],
			sizeof(channelVolumes[i]));
	}
}


status_t
CortexDeviceControllable::GetParameterValue(int32 id,
	bigtime_t* lastChangeTime, void* value, size_t* ioSize)
{
	if (value == NULL || ioSize == NULL)
		return B_BAD_VALUE;

	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	if (id == kDeviceVolume && *ioSize >= sizeof(float)) {
		float master = 0.0f;
		if (graph->GetDeviceVolume(fId, &master, NULL) != B_OK)
			return B_ERROR;
		*(float*)value = master;
		*ioSize = sizeof(float);
		if (lastChangeTime != NULL)
			*lastChangeTime = system_time();
		return B_OK;
	}

	if (id == kDeviceMute && *ioSize >= sizeof(int32)) {
		float master = 0.0f;
		bool mute = false;
		if (graph->GetDeviceVolume(fId, &master, &mute) != B_OK)
			return B_ERROR;
		*(int32*)value = mute ? 1 : 0;
		*ioSize = sizeof(int32);
		if (lastChangeTime != NULL)
			*lastChangeTime = system_time();
		return B_OK;
	}

	if (id >= kChannelVolumeBase && id < kChannelVolumeBase + kMaxDeviceChannels
			&& *ioSize >= sizeof(float)) {
		float volumes[kMaxDeviceChannels];
		size_t count = 0;
		if (graph->GetDeviceChannelVolumes(fId, volumes,
				kMaxDeviceChannels, &count) == B_OK) {
			const size_t index = (size_t)(id - kChannelVolumeBase);
			if (index < count) {
				*(float*)value = volumes[index];
				*ioSize = sizeof(float);
				if (lastChangeTime != NULL)
					*lastChangeTime = system_time();
				return B_OK;
			}
		}
	}

	return BControllable::GetParameterValue(id, lastChangeTime, value, ioSize);
}


status_t
CortexDeviceControllable::SetParameterValue(int32 id, bigtime_t changeTime,
	const void* value, size_t size)
{
	if (value == NULL || size == 0)
		return B_BAD_VALUE;

	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	float floatValue = 0.0f;
	int32 intValue = 0;
	if (size == sizeof(int32))
		intValue = *(const int32*)value;
	if (size >= sizeof(float))
		floatValue = *(const float*)value;

	status_t status = B_OK;
	if (id == kDeviceVolume) {
		status = graph->SetDeviceVolume(fId, floatValue);
	} else if (id == kDeviceMute) {
		status = graph->SetDeviceMute(fId, intValue != 0);
	} else if (id >= kChannelVolumeBase
			&& id < kChannelVolumeBase + kMaxDeviceChannels) {
		float volumes[kMaxDeviceChannels];
		size_t count = 0;
		memset(volumes, 0, sizeof(volumes));
		graph->GetDeviceChannelVolumes(fId, volumes, kMaxDeviceChannels,
			&count);
		if (count > kMaxDeviceChannels)
			count = kMaxDeviceChannels;
		const size_t index = (size_t)(id - kChannelVolumeBase);
		if (index < kMaxDeviceChannels) {
			if (index >= count)
				count = index + 1;
			volumes[index] = floatValue;
			status = graph->SetDeviceChannelVolumes(fId, volumes, count);
		}
	} else if (id == kDeviceProfile) {
		status = graph->SetDeviceProfile(fId, intValue);
	} else if (id == kDevicePort) {
		status = graph->SetDevicePort(fId, intValue);
	} else {
		status = B_BAD_VALUE;
	}

	if (status != B_OK)
		return status;

	status_t stored = BControllable::SetParameterValue(id, changeTime, value,
		size);
	if (stored != B_OK)
		return stored;

	BroadcastChangedParameter(id);
	return B_OK;
}
