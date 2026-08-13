/*
 * Copyright 2006-2008, Haiku, Inc. All Rights Reserved.
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#ifndef BLUETOOTH_STATUS_H
#define BLUETOOTH_STATUS_H


#include <image.h>


extern const char* kSignature;
extern const char* kDeskbarSignature;
extern const char* kDeskbarItemName;

status_t our_image(image_info& image);

#endif	// BLUETOOTH_STATUS_H