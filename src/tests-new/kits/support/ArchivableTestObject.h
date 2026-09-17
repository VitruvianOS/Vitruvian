// An archivable class implemented in the test executable itself, used by the
// find_instantiation_func() and instantiate_object() tests.
//
// Instantiate() must be defined out of line, or the compiler may not emit a
// symbol for it and the instantiation lookup finds nothing. The test target
// also has to export its symbols, see CMakeLists.txt.

#ifndef _ARCHIVABLE_TEST_OBJECT_H
#define _ARCHIVABLE_TEST_OBJECT_H

#include <Archivable.h>
#include <Message.h>


class TIOTest : public BArchivable {
public:
								TIOTest(int32 i);
								TIOTest(BMessage* archive);

			int32				GetData() const { return fData; }

			status_t			Archive(BMessage* archive,
									bool deep = true) const;
	static	TIOTest*			Instantiate(BMessage* archive);

private:
			int32				fData;
};


#endif	// _ARCHIVABLE_TEST_OBJECT_H
