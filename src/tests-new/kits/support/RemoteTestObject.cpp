// See RemoteTestObject.h

#include "RemoteTestObject.h"


TRemoteTestObject::TRemoteTestObject(int32 i)
	:
	fData(i)
{
}


TRemoteTestObject::TRemoteTestObject(BMessage* archive)
	:
	fData(archive->FindInt32("TRemoteTestObject::data"))
{
}


status_t
TRemoteTestObject::Archive(BMessage* archive, bool deep) const
{
	status_t err = archive->AddString("class", "TRemoteTestObject");
	if (err == B_OK)
		err = archive->AddInt32("TRemoteTestObject::data", fData);

	return err;
}


TRemoteTestObject*
TRemoteTestObject::Instantiate(BMessage* archive)
{
	if (validate_instantiation(archive, "TRemoteTestObject"))
		return new TRemoteTestObject(archive);

	return NULL;
}
