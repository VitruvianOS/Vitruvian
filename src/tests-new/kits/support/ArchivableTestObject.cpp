// See ArchivableTestObject.h

#include "ArchivableTestObject.h"


TIOTest::TIOTest(int32 i)
	:
	fData(i)
{
}


TIOTest::TIOTest(BMessage* archive)
	:
	fData(archive->FindInt32("TIOTest::data"))
{
}


status_t
TIOTest::Archive(BMessage* archive, bool deep) const
{
	status_t err = archive->AddString("class", "TIOTest");
	if (err == B_OK)
		err = archive->AddInt32("TIOTest::data", fData);

	return err;
}


TIOTest*
TIOTest::Instantiate(BMessage* archive)
{
	if (validate_instantiation(archive, "TIOTest"))
		return new TIOTest(archive);

	return NULL;
}
