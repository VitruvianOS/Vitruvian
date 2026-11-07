/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */
#ifndef _DEVICE_LIST_ITEM_H
#define _DEVICE_LIST_ITEM_H

#include <ListItem.h>
#include <String.h>

#include "LsDevice.h"

// One discovered peer in the device list. Keeps the wire-level device
// info plus the address:port needed to talk to it.
class DeviceListItem : public BListItem {
public:
						DeviceListItem(const BString& alias,
							const BString& typeName,
							const BString& model,
							const BString& fingerprint,
							const BString& address,
							uint16 port,
							const BString& protocol,
							bool download,
							const BString& version);

			// Refresh in place on a repeat announce; replacing the
			// item would drop the user's selection.
			void		UpdateDevice(const BString& alias,
							const BString& typeName,
							const BString& model,
							const BString& fingerprint,
							const BString& address,
							uint16 port,
							const BString& protocol,
							bool download,
							const BString& version);

	virtual	void		DrawItem(BView* owner, BRect frame,
							bool complete = false);
	virtual	void		Update(BView* owner, const BFont* font);

			const BString&	Alias() const { return fAlias; }
			const BString&	Address() const { return fAddress; }
			uint16		Port() const { return fPort; }
			const LocalSend::LsDevice&	Device() const { return fDevice; }

private:
			BString		fAlias;
			BString		fTypeName;
			BString		fModel;
			BString		fFingerprint;
			BString		fAddress;
			uint16		fPort;
			BString		fProtocol;
			bool		fDownload;
			BString		fVersion;
			LocalSend::LsDevice	fDevice;
			float		fMaxTextWidth;
};

#endif // _DEVICE_LIST_ITEM_H
