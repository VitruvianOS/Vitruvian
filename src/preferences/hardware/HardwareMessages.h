/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _HARDWARE_MESSAGES_H
#define _HARDWARE_MESSAGES_H


// Message codes shared by the Hardware preflet halves.


enum {
	kMsgDevicesUpdated = 'HWup',	// scanner finished a (re)scan
	kMsgRescan = 'HWrs',			// ask the scanner to run again
	kMsgHelperResult = 'HWhp',		// polkit helper finished
									// "ok" bool, "text" string
	kMsgListSelection = 'HWsl',		// a tab's selection changed
};


#endif	// _HARDWARE_MESSAGES_H
