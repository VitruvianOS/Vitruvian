/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _DEVICE_LIST_ITEM_H
#define _DEVICE_LIST_ITEM_H


#include <StringItem.h>

#include "HardwareModel.h"


// Outline items for the Hardware preflet device trees: a category
// header at level 0 and a device leaf at level 1 carrying its record.


class DeviceCategoryItem : public BStringItem {
public:
							DeviceCategoryItem(
								Hardware::DeviceCategory
									category);

			Hardware::DeviceCategory
							Category() const { return fCategory; }

	virtual	void			DrawItem(BView* owner, BRect frame,
								bool complete = false);

private:
			Hardware::DeviceCategory
							fCategory;
};


class DeviceItem : public BStringItem {
public:
							DeviceItem(
								const Hardware::DeviceRecord&
									record,
								const char* name);

			const Hardware::DeviceRecord&
							Record() const { return fRecord; }

private:
			Hardware::DeviceRecord
							fRecord;
};


#endif	// _DEVICE_LIST_ITEM_H
