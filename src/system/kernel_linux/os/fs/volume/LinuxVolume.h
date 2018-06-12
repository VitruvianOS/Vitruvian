// ----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//
//  File Name:		Directory.cpp
//
//	Description:	BVolume class
// ----------------------------------------------------------------------

#ifndef _LINUX_VOLUME_H
#define _LINUX_VOLUME_H

#include <sys/types.h>
#include <string>

#include <fs_info.h>
#include <Mime.h>
#include <StorageDefs.h>
#include <SupportDefs.h>
#include <Path.h>

#include <mntent.h>


class LinuxVolume {
public:
	LinuxVolume(struct mntent* inMountEntry);
	virtual ~LinuxVolume();

	status_t InitCheck() const;

	dev_t Device() const;
	
private:
	dev_t		fDevice;
	status_t	fCStatus;
};

#endif	// _LINUX_VOLUME_H
