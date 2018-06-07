// ----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//
//  File Name:		Volume.cpp
//
//	Description:	BVolume class
// ----------------------------------------------------------------------
/*!
	\file Volume.h
	BVolume implementation.
*/

#include <errno.h>

#include <Bitmap.h>
#include <Directory.h>
#include <fs_info.h>
#include <kernel_interface.h>
#include <Node.h>
#include <Volume.h>


#ifdef USE_OPENBEOS_NAMESPACE
namespace OpenBeOS {
#endif



/*!
	\class BVolume
	\brief Represents a disk volume
	
	Provides an interface for querying information about a volume.

	The class is a simple wrapper for a \c dev_t and the function
	fs_stat_dev. The only exception is the method is SetName(), which
	sets the name of the volume.

	\author Vincent Dominguez
	\author <a href='mailto:bonefish@users.sf.net'>Ingo Weinhold</a>
	
	\version 0.0.0
*/

/*!	\var dev_t BVolume::fDevice
	\brief The volume's device ID.
*/

/*!	\var dev_t BVolume::fCStatus
	\brief The object's initialization status.
*/

// constructor
/*!	\brief Creates an uninitialized BVolume.

	InitCheck() will return \c B_NO_INIT.
*/
BVolume::BVolume()
	: fDevice((dev_t)-1),
	  fCStatus(B_NO_INIT)
{
}


// constructor
/*!	\brief Creates a BVolume and initializes it to the volume specified
		   by the supplied device ID.

	InitCheck() should be called to check whether the initialization was
	successful.

	\param device The device ID of the volume.
*/
BVolume::BVolume(dev_t device)
	: fDevice((dev_t)-1),
	  fCStatus(B_NO_INIT)
{
	SetTo(device);
}


// copy constructor
/*!	\brief Creates a BVolume and makes it a clone of the supplied one.

	Afterwards the object refers to the same device the supplied object
	does. If the latter is not properly initialized, this object isn't
	either.

	\param volume The volume object to be cloned.
*/
BVolume::BVolume(const BVolume &volume)
	: fDevice(volume.fDevice),
	  fCStatus(volume.fCStatus)
{
}

#ifndef __APPLE__
BVolume::BVolume(struct mntent* inMountEntry)
{
	char* deviceOptionsList;

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

	// Get the properties

	mPropertiesLoaded = true;

	mIsShared = false;	// FIXME
	mIsRemovable = false;	// FIXME
	mIsReadOnly = ( hasmntopt(inMountEntry, MNTOPT_RO) != NULL );
	mIsPersistent = true;	// FIXME
	mCapacity = 0L;	// FIXME
	mFreeBytes = 0L;	// FIXME
	mName = "Bocephus";	// FIXME
	mDevicePath.SetTo(inMountEntry->mnt_fsname);
	mMountPath.SetTo(inMountEntry->mnt_dir);

	fCStatus = (fDevice == -1) ? -1 : 0;
}
#endif

// destructor
/*!	\brief Frees all resources associated with the object.

	Does nothing.
*/
BVolume::~BVolume()
{
}

// InitCheck
/*!	\brief Returns the result of the last initialization.
	\return
	- \c B_OK: The object is properly initialized.
	- an error code otherwise
*/
status_t
BVolume::InitCheck(void) const
{	
	return fCStatus;
}


// SetTo
/*!	\brief Re-initializes the object to refer to the volume specified by
		   the supplied device ID.
	\param device The device ID of the volume.
	\param
	- \c B_OK: Everything went fine.
	- an error code otherwise
*/
status_t
BVolume::SetTo(dev_t device)
{
	// uninitialize
	Unset();
	// check the parameter
	status_t error = (device >= 0 ? B_OK : B_BAD_VALUE);
	if (error == B_OK) {
//FIXME
	}

	// set the new value
	if (error == B_OK)	
		fDevice = device;
	mPropertiesLoaded = false;

	// set the init status variable
	fCStatus = error;
	return fCStatus;
}


// Unset
/*!	\brief Uninitialized the BVolume.
*/
void
BVolume::Unset()
{
	fDevice = (dev_t)-1;
	fCStatus = B_NO_INIT;
	mPropertiesLoaded = false;
}

// Device
/*!	\brief Returns the device ID of the volume the object refers to.
	\return Returns the device ID of the volume the object refers to
			or -1, if the object is not properly initialized.
*/
dev_t
BVolume::Device() const 
{
	return fDevice;
}

// GetRootDirectory
/*!	\brief Returns the root directory of the volume referred to by the object.
	\param directory A pointer to a pre-allocated BDirectory to be initialized
		   to the volume's root directory.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a directory or the object is not properly
	  initialized.
	- another error code
*/
status_t
BVolume::GetRootDirectory(BDirectory *directory) const
{
	// check parameter and initialization
	status_t error = (directory && InitCheck() == B_OK ? B_OK : B_BAD_VALUE);
	if (!mPropertiesLoaded)
		_LoadVolumeProperties();

	return error;
}


// Capacity
/*!	\brief Returns the volume's total storage capacity.
	\return
	- The volume's total storage capacity (in bytes), when the object is
	  properly initialized.
	- \c B_BAD_VALUE otherwise.
*/
off_t
BVolume::Capacity() const
{
	if (!mPropertiesLoaded)
		_LoadVolumeProperties();

	return mCapacity;
}


// FreeBytes
/*!	\brief Returns the amount of storage that's currently unused on the
		   volume (in bytes).
	\return
	- The amount of storage that's currently unused on the volume (in bytes),
	  when the object is properly initialized.
	- \c B_BAD_VALUE otherwise.
*/
off_t
BVolume::FreeBytes() const
{
	if (!mPropertiesLoaded)
		_LoadVolumeProperties();

	return mFreeBytes;
}


// GetName
/*!	\brief Returns the name of the volume.

	The name of the volume is copied into the provided buffer.

	\param name A pointer to a pre-allocated character buffer of size
		   \c B_FILE_NAME_LENGTH or larger into which the name of the
		   volume shall be written.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a name or the object is not properly
	  initialized.
	- another error code
*/
status_t
BVolume::GetName(char *name) const
{
	if (!mPropertiesLoaded)
		_LoadVolumeProperties();

	if (mMountPath.Path() != NULL)
		strcpy(name, mMountPath.Path());
	else if (mDevicePath.Path() != NULL)
		strcpy(name, mDevicePath.Path());
	else
		return -1;

	return B_OK;
}


// SetName
/*!	\brief Sets the name of the volume referred to by this object.
	\param name The volume's new name. Must not be longer than
		   \c B_FILE_NAME_LENGTH (including the terminating null).
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a name or the object is not properly
	  initialized.
	- another error code
*/
status_t
BVolume::SetName(const char *name)
{
	// check initialization
	status_t error = (InitCheck() == B_OK ? B_OK : B_BAD_VALUE);
	mName = name;
	return error;
}

// GetIcon
/*!	\brief Returns the icon of the volume.
	\param icon A pointer to a pre-allocated BBitmap of the correct dimension
		   to store the requested icon (16x16 for the mini and 32x32 for the
		   large icon).
	\param which Specifies the size of the icon to be retrieved:
		   \c B_MINI_ICON for the mini and \c B_LARGE_ICON for the large icon.
*/
status_t
BVolume::GetIcon(BBitmap *icon, icon_size which) const
{
	// check initialization
	status_t error = (InitCheck() == B_OK ? B_OK : B_BAD_VALUE);
	// get FS stat
	return B_ERROR;
}

// IsRemovable
/*!	\brief Returns whether the volume is removable.
	\return \c true, when the object is properly initialized and the
	referred to volume is removable, \c false otherwise.
*/
bool
BVolume::IsRemovable() const
{
	// check initialization
	status_t error = (InitCheck() == B_OK ? B_OK : B_BAD_VALUE);
	// get FS stat
	if (!mPropertiesLoaded)
		_LoadVolumeProperties();

	return mIsRemovable;
}


// IsReadOnly
/*!	\brief Returns whether the volume is read only.
	\return \c true, when the object is properly initialized and the
	referred to volume is read only, \c false otherwise.
*/
bool
BVolume::IsReadOnly(void) const
{
	if (!mPropertiesLoaded)
		_LoadVolumeProperties();

	return mIsReadOnly;
}


// IsPersistent
/*!	\brief Returns whether the volume is persistent.
	\return \c true, when the object is properly initialized and the
	referred to volume is persistent, \c false otherwise.
*/
bool
BVolume::IsPersistent(void) const
{
	if (!mPropertiesLoaded)
		_LoadVolumeProperties();

	return mIsPersistent;
}


// IsShared
/*!	\brief Returns whether the volume is shared.
	\return \c true, when the object is properly initialized and the
	referred to volume is shared, \c false otherwise.
*/
bool
BVolume::IsShared(void) const
{
	// check initialization
	if (!mPropertiesLoaded)
		_LoadVolumeProperties();

	return mIsShared;
}


// KnowsMime
/*!	\brief Returns whether the volume supports MIME types.
	\return \c true, when the object is properly initialized and the
	referred to volume supports MIME types, \c false otherwise.
*/
bool
BVolume::KnowsMime(void) const
{
	return false;
}

// KnowsAttr
/*!	\brief Returns whether the volume supports attributes.
	\return \c true, when the object is properly initialized and the
	referred to volume supports attributes, \c false otherwise.
*/
bool
BVolume::KnowsAttr(void) const
{
	return false;
}

// KnowsQuery
/*!	\brief Returns whether the volume supports queries.
	\return \c true, when the object is properly initialized and the
	referred to volume supports queries, \c false otherwise.
*/
bool
BVolume::KnowsQuery(void) const
{
	return false;
}

// ==
/*!	\brief Returns whether two BVolume objects are equal.

	Two volume objects are said to be equal, if they either are both
	uninitialized, or both are initialized and refer to the same volume.

	\param volume The object to be compared with.
	\result \c true, if this object and the supplied one are equal, \c false
			otherwise.
*/
bool
BVolume::operator==(const BVolume &volume) const
{
	return (InitCheck() != B_OK && volume.InitCheck() != B_OK
			|| fDevice == volume.fDevice);
}

// !=
/*!	\brief Returns whether two BVolume objects are unequal.

	Two volume objects are said to be equal, if they either are both
	uninitialized, or both are initialized and refer to the same volume.

	\param volume The object to be compared with.
	\result \c true, if this object and the supplied one are unequal, \c false
			otherwise.
*/
bool
BVolume::operator!=(const BVolume &volume) const
{
	return !(*this == volume);
}

// =
/*!	\brief Assigns another BVolume object to this one.

	This object is made an exact clone of the supplied one.

	\param volume The volume from which shall be assigned.
	\return A reference to this object.
*/
BVolume&
BVolume::operator=(const BVolume &volume)
{
	if (&volume != this) {
		fDevice = volume.fDevice;

		mPropertiesLoaded = volume.mPropertiesLoaded;
		mIsShared = volume.mIsShared;
		mIsRemovable = volume.mIsRemovable;
		mIsReadOnly = volume.mIsReadOnly;
		mIsPersistent = volume.mIsPersistent;
		mCapacity = volume.mCapacity;
		mFreeBytes = volume.mFreeBytes;

		mDevicePath = volume.mDevicePath;
		mMountPath = volume.mMountPath;
	}
	return *this;
}


void		BVolume::_LoadVolumeProperties() const
{
	mPropertiesLoaded = true;
}

// FBC 
void BVolume::_TurnUpTheVolume1() {} 
void BVolume::_TurnUpTheVolume2() {} 
void BVolume::_TurnUpTheVolume3() {} 
void BVolume::_TurnUpTheVolume4() {} 
void BVolume::_TurnUpTheVolume5() {} 
void BVolume::_TurnUpTheVolume6() {} 
void BVolume::_TurnUpTheVolume7() {} 
void BVolume::_TurnUpTheVolume8() {}

#ifdef USE_OPENBEOS_NAMESPACE
}
#endif
