/*
 * Copyright 2018, Dario Casalinuovo.
 * Distributed under the terms of the MIT License.
 */

#ifndef _LINUX_VOLUME_H
#define _LINUX_VOLUME_H

#include <sys/types.h>

#include <StorageDefs.h>
#include <String.h>
#include <SupportDefs.h>


class LinuxVolume {
public:
				LinuxVolume(struct mntent* mountEntry, dev_t id);
	virtual 	~LinuxVolume();

	status_t	InitCheck() const;

	const char* Name() const;

	dev_t		Device() const;

private:
	dev_t		fDevice;
	BString		fName;

	status_t	fCStatus;
};

#endif	// _LINUX_VOLUME_H
