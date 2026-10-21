/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#ifndef _CORTEX_DEVICE_CONTROLS_H
#define _CORTEX_DEVICE_CONTROLS_H


#include <media2/Controllable.h>
#include <media2/MediaClientDefs.h>


// A control panel for a PipeWire device node, layered on the graph's
// existing volume, mute, profile and port calls: master and per-channel
// volume, mute, card profile and route/port.
class CortexDeviceControllable : public BControllable {
public:
								CortexDeviceControllable(media_client_id id);
	virtual						~CortexDeviceControllable();

			media_client_id		DeviceId() const { return fId; }

	virtual	status_t			GetParameterValue(int32 id,
									bigtime_t* lastChangeTime, void* value,
									size_t* ioSize);
	virtual	status_t			SetParameterValue(int32 id,
									bigtime_t changeTime, const void* value,
									size_t size);

private:
			void				_BuildWeb();

			media_client_id		fId;
};


#endif // _CORTEX_DEVICE_CONTROLS_H
