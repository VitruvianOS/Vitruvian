/*
 * Copyright 2026, Vitruvian Project.
 * DeviceListView — list of devices (sinks or sources) with default marker.
 * Used by Output/Input sections.
 * Distributed under the terms of the MIT License.
 */

#ifndef DEVICE_LIST_VIEW_H
#define DEVICE_LIST_VIEW_H


#include <ListView.h>
#include <StringItem.h>

#include "MediaMessages.h"


class DeviceListView : public BListView {
public:
							DeviceListView(const char* name, bool isOutput);
	virtual					~DeviceListView();

			void			Refresh();






			void			SetTargetForMessages(BHandler* target)
								{ SetTarget(target); }

			bool			IsOutput() const { return fIsOutput; }

private:
			bool			fIsOutput;
};


class DeviceListItem : public BStringItem {
public:
							DeviceListItem(uint32 deviceId, const char* name,
								bool isDefault);
			uint32			DeviceId() const { return fDeviceId; }
			bool			IsDefault() const { return fIsDefault; }
			void			SetDefault(bool d) { fIsDefault = d; }
			virtual	void	DrawItem(BView* owner, BRect frame,
								bool complete = true) override;
private:
			uint32			fDeviceId;
			bool			fIsDefault;
};


#endif
