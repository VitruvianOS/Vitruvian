// An archivable class built into a separate add-on, so that the
// instantiate_object() tests can exercise looking a class up in another image.
//
// The add-on carries the MIME signature in RemoteTestObject.rdef.

#ifndef _REMOTE_TEST_OBJECT_H
#define _REMOTE_TEST_OBJECT_H

#include <Archivable.h>
#include <Message.h>


class TRemoteTestObject : public BArchivable {
public:
								TRemoteTestObject(int32 i);
								TRemoteTestObject(BMessage* archive);

			int32				GetData() const { return fData; }

			status_t			Archive(BMessage* archive,
									bool deep = true) const;
	static	TRemoteTestObject*	Instantiate(BMessage* archive);

private:
			int32				fData;
};


#endif	// _REMOTE_TEST_OBJECT_H
