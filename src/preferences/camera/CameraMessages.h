/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef CAMERA_MESSAGES_H
#define CAMERA_MESSAGES_H


enum {
	kMsgCameraDeviceSelected = 'CAdc',
	kMsgCameraFrame          = 'CAdf',
	kMsgCameraRefresh        = 'CAdr',
	kMsgCameraControlChanged = 'CAct',
	kMsgCameraResetControls  = 'CArs',
	kMsgCameraSetDefault     = 'CAcs',
	kMsgPWDevicesChanged     = 'PWDC',
	kMsgPWDefaultChanged     = 'PWDF'
};


#endif
