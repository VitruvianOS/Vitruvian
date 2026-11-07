/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _DEVICE_CLASSIFIER_H
#define _DEVICE_CLASSIFIER_H


#include "HardwareModel.h"


// Maps a DeviceRecord onto a tab and an outline category. Pure string
// logic so the host tests can pin the grouping without a udev context.


namespace Hardware {


struct Classification {
	DeviceTab		tab;
	DeviceCategory	category;
	bool			wanted;

						Classification();
};


Classification
ClassifyRecord(const DeviceRecord& record);


}	// namespace Hardware


#endif	// _DEVICE_CLASSIFIER_H
