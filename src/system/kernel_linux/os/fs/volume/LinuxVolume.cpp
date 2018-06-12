// ----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//
//  File Name:		Volume.cpp
//
//	Description:	BVolume class
// ----------------------------------------------------------------------

#include "LinuxVolume.h"

#include <errno.h>

#include <Bitmap.h>
#include <Directory.h>
#include <fs_info.h>
#include <Node.h>


LinuxVolume::LinuxVolume(struct mntent* inMountEntry)
{
	char* deviceOptionsList;

	printf("%s\n", inMountEntry->mnt_opts);

	// Extract the device number

	deviceOptionsList = strstr(inMountEntry->mnt_opts, "dev=");

	if (deviceOptionsList)
	{
		int offset = 4;

		if (deviceOptionsList[5] == 'x' || deviceOptionsList[5] == 'X')
			offset += 2;

		fDevice = atoi(deviceOptionsList + offset);
	}
	else
		fDevice = (dev_t) -1;

	fCStatus = (fDevice == -1) ? -1 : 0;
}


// destructor
/*!	\brief Frees all resources associated with the object.

	Does nothing.
*/
LinuxVolume::~LinuxVolume()
{
}

// InitCheck
/*!	\brief Returns the result of the last initialization.
	\return
	- \c B_OK: The object is properly initialized.
	- an error code otherwise
*/
status_t
LinuxVolume::InitCheck(void) const
{	
	return fCStatus;
}


dev_t
LinuxVolume::Device() const 
{
	return fDevice;
}
