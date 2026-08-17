/*
 * Copyright 2026, Vitruvian Project.
 * Shared message constants for Media preferences.
 * Distributed under the terms of the MIT License.
 */

#ifndef MEDIA_MESSAGES_H
#define MEDIA_MESSAGES_H


#include <MediaMessagingDefs.h>



const uint32 kMsgSectionChanged = 'MScg';


const uint32 kMsgRefresh        = 'MSrf';


const uint32 kMsgDeviceSelected = 'MSds';


const uint32 kMsgHardwareDeviceSelected  = 'MShd';
const uint32 kMsgHardwareProfileSelected = 'MShp';


const uint32 kMsgStreamSelected = 'MSss';
const uint32 kMsgStreamVolume   = 'MSsv';
const uint32 kMsgStreamMute     = 'MSsm';
const uint32 kMsgStreamRouted   = 'MSsr';


const uint32 kMsgPWDevicesChanged       = 'PWDC';
const uint32 kMsgPWStreamsChanged       = 'PWSC';
const uint32 kMsgPWDefaultChanged       = 'PWDF';
const uint32 kMsgPWDeviceVolumeChanged  = 'PWVC';
const uint32 kMsgPWDevicePortChanged    = 'PWPC';
const uint32 kMsgPWDeviceProfileChanged = 'PWPF';


#endif
