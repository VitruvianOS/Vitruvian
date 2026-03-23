// StatableTest.cpp

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cppunit/TestCaller.h>
#include <cppunit/TestSuite.h>

#include <Statable.h>
#include <Entry.h>
#include <Node.h>
#include <Volume.h>

#include "StatableTest.h"

// setUp
void
StatableTest::setUp()
{
	BasicTest::setUp();
}

// tearDown
void
StatableTest::tearDown()
{
	BasicTest::tearDown();
}

// GetStatTest
void
StatableTest::GetStatTest()
{
	TestStatables testEntries;
	BStatable *statable;
	string entryName;
	// existing entries
	NextSubTest();
	CreateROStatables(testEntries);
	for (testEntries.rewind(); testEntries.getNext(statable, entryName); ) {
		struct stat st1, st2;
		CPPUNIT_ASSERT( statable->GetStat(&st1) == B_OK );
		CPPUNIT_ASSERT( lstat(entryName.c_str(), &st2) == 0 );
		CPPUNIT_ASSERT( st1 == st2 );
	}
	testEntries.delete_all();
	// uninitialized objects
	NextSubTest();
	CreateUninitializedStatables(testEntries);
	for (testEntries.rewind(); testEntries.getNext(statable, entryName); ) {
		struct stat st1;
		CPPUNIT_ASSERT( statable->GetStat(&st1) == B_NO_INIT );
	}
	testEntries.delete_all();
	// bad args
	NextSubTest();
	CreateROStatables(testEntries);
	for (testEntries.rewind(); testEntries.getNext(statable, entryName); )
		CPPUNIT_ASSERT( statable->GetStat(NULL) != B_OK );
	testEntries.delete_all();
}

// IsXYZTest
void
StatableTest::IsXYZTest()
{
	TestStatables testEntries;
	BStatable *statable;
	string entryName;
	// existing entries
	NextSubTest();
	CreateROStatables(testEntries);
	for (testEntries.rewind(); testEntries.getNext(statable, entryName); ) {
		struct stat st;
		CPPUNIT_ASSERT( lstat(entryName.c_str(), &st) == 0 );
		CPPUNIT_ASSERT( statable->IsDirectory() == S_ISDIR(st.st_mode) );
		CPPUNIT_ASSERT( statable->IsFile() == S_ISREG(st.st_mode) );
		CPPUNIT_ASSERT( statable->IsSymLink() == S_ISLNK(st.st_mode) );
	}
	testEntries.delete_all();
	// uninitialized objects
	NextSubTest();
	CreateUninitializedStatables(testEntries);
	for (testEntries.rewind(); testEntries.getNext(statable, entryName); ) {
		CPPUNIT_ASSERT( statable->IsDirectory() == false );
		CPPUNIT_ASSERT( statable->IsFile() == false );
		CPPUNIT_ASSERT( statable->IsSymLink() == false );
	}
	testEntries.delete_all();
}

// GetXYZTest
void
StatableTest::GetXYZTest()
{
	TestStatables testEntries;
	BStatable *statable;
	string entryName;
	// test with existing entries
	NextSubTest();
	CreateROStatables(testEntries);
	for (testEntries.rewind(); testEntries.getNext(statable, entryName); ) {
		struct stat st;
		node_ref ref;
		uid_t owner;
		gid_t group;
		mode_t perms;
		off_t size;
		time_t mtime;
		time_t ctime;
// R5: access time unused
#if !TEST_R5 && !TEST_OBOS /* !!!POSIX ONLY!!! */
		time_t atime;
#endif
		BVolume volume;
		// BNode follows symlinks on Linux (O_NOTRAVERSE = O_NOCTTY there),
		// so compare against the resolved target via stat(), not lstat().
		CPPUNIT_ASSERT( stat(entryName.c_str(), &st) == 0 );
		CPPUNIT_ASSERT( statable->GetNodeRef(&ref) == B_OK );
		CPPUNIT_ASSERT( statable->GetOwner(&owner) == B_OK );
		CPPUNIT_ASSERT( statable->GetGroup(&group) == B_OK );
		CPPUNIT_ASSERT( statable->GetPermissions(&perms) == B_OK );
		CPPUNIT_ASSERT( statable->GetSize(&size) == B_OK );
		CPPUNIT_ASSERT( statable->GetModificationTime(&mtime) == B_OK );
		CPPUNIT_ASSERT( statable->GetCreationTime(&ctime) == B_OK );
#if !TEST_R5 && !TEST_OBOS /* !!!POSIX ONLY!!! */
		CPPUNIT_ASSERT( statable->GetAccessTime(&atime) == B_OK );
#endif
		CPPUNIT_ASSERT( statable->GetVolume(&volume) == B_OK );
		CPPUNIT_ASSERT( ref.dereference().device == st.st_dev && ref.dereference().node == st.st_ino );
		// Direct operator== handles virtual node_ref vs physical (dev, ino) pair
		CPPUNIT_ASSERT( ref == node_ref(st.st_dev, st.st_ino) );
		CPPUNIT_ASSERT( owner == st.st_uid );
		CPPUNIT_ASSERT( group == st.st_gid );
// R5: returns not only the permission bits, so we need to filter for the test
		CPPUNIT_ASSERT( (perms & S_IUMSK) == (st.st_mode & S_IUMSK) );
		CPPUNIT_ASSERT( size == st.st_size );
		CPPUNIT_ASSERT( mtime == st.st_mtime );
#ifndef __VOS__
		CPPUNIT_ASSERT( perms == (st.st_mode & S_IUMSK) );
		CPPUNIT_ASSERT( ctime == st.st_crtime );
#endif
#if !TEST_R5 && !TEST_OBOS /* !!!POSIX ONLY!!! */
		CPPUNIT_ASSERT( atime == st.st_atime );
#endif
		CPPUNIT_ASSERT( volume == BVolume(st.st_dev) );
	}
	testEntries.delete_all();
	// cross-vref: two different vref IDs from two independent opens of the same
	// file must compare equal via node_ref::operator==
	NextSubTest();
	{
		int fd1 = open("/tmp", O_RDONLY | O_DIRECTORY);
		int fd2 = open("/tmp", O_RDONLY | O_DIRECTORY);
		CPPUNIT_ASSERT( fd1 >= 0 && fd2 >= 0 );
		node_ref virtRef1(fd1);
		node_ref virtRef2(fd2);
		// Different vref IDs (two independent create_vref calls)
		CPPUNIT_ASSERT( virtRef1.node != virtRef2.node );
		// operator== dereferences both and finds the same physical inode
		CPPUNIT_ASSERT( virtRef1 == virtRef2 );
		CPPUNIT_ASSERT( !(virtRef1 != virtRef2) );
		CPPUNIT_ASSERT( virtRef1.dereference() == virtRef2.dereference() );
		// virtual == physical for the same inode
		struct stat st;
		CPPUNIT_ASSERT( lstat("/tmp", &st) == 0 );
		node_ref physRef(st.st_dev, st.st_ino);
		CPPUNIT_ASSERT( virtRef1 == physRef );
		CPPUNIT_ASSERT( physRef == virtRef1 );
		CPPUNIT_ASSERT( !(virtRef1 != physRef) );
		close(fd1);
		close(fd2);
	}
	// test with uninitialized objects
	NextSubTest();
	CreateUninitializedStatables(testEntries);
	for (testEntries.rewind(); testEntries.getNext(statable, entryName); ) {
		node_ref ref;
		uid_t owner;
		gid_t group;
		mode_t perms;
		off_t size;
		time_t mtime;
		time_t ctime;
		time_t atime;
		BVolume volume;
		CPPUNIT_ASSERT( statable->GetNodeRef(&ref) == B_NO_INIT );
		CPPUNIT_ASSERT( statable->GetOwner(&owner) == B_NO_INIT );
		CPPUNIT_ASSERT( statable->GetGroup(&group) == B_NO_INIT );
		CPPUNIT_ASSERT( statable->GetPermissions(&perms) == B_NO_INIT );
		CPPUNIT_ASSERT( statable->GetSize(&size) == B_NO_INIT );
		CPPUNIT_ASSERT( statable->GetModificationTime(&mtime) == B_NO_INIT );
		CPPUNIT_ASSERT( statable->GetCreationTime(&ctime) == B_NO_INIT );
		CPPUNIT_ASSERT( statable->GetAccessTime(&atime) == B_NO_INIT );
		CPPUNIT_ASSERT( statable->GetVolume(&volume) == B_NO_INIT );
	}
	testEntries.delete_all();
	// bad args
	NextSubTest();
	CreateROStatables(testEntries);
	for (testEntries.rewind(); testEntries.getNext(statable, entryName); ) {
// R5: crashs, if passing NULL to any of these methods
#if !TEST_R5
		CPPUNIT_ASSERT( statable->GetNodeRef(NULL) == B_BAD_VALUE );
		CPPUNIT_ASSERT( statable->GetOwner(NULL)  == B_BAD_VALUE );
		CPPUNIT_ASSERT( statable->GetGroup(NULL)  == B_BAD_VALUE );
		CPPUNIT_ASSERT( statable->GetPermissions(NULL)  == B_BAD_VALUE );
		CPPUNIT_ASSERT( statable->GetSize(NULL)  == B_BAD_VALUE );
		CPPUNIT_ASSERT( statable->GetModificationTime(NULL)  == B_BAD_VALUE );
		CPPUNIT_ASSERT( statable->GetCreationTime(NULL)  == B_BAD_VALUE );
		CPPUNIT_ASSERT( statable->GetAccessTime(NULL)  == B_BAD_VALUE );
		CPPUNIT_ASSERT( statable->GetVolume(NULL)  == B_BAD_VALUE );
#endif
	}
	testEntries.delete_all();
}

// SetXYZTest
void
StatableTest::SetXYZTest()
{
	TestStatables testEntries;
	BStatable *statable;
	string entryName;
	// test with existing entries
	NextSubTest();
	CreateRWStatables(testEntries);
	for (testEntries.rewind(); testEntries.getNext(statable, entryName); ) {
		struct stat st;
		uid_t owner = 0xdad;
		gid_t group = 0xdee;
		mode_t perms = 0x0ab;	// -w- r-x -wx	-- unusual enough? ;-)
		time_t mtime = 1234567;
		time_t ctime = 654321;
// R5: access time unused
#if !TEST_R5 && !TEST_OBOS /* !!!POSIX ONLY!!! */
		time_t atime = 2345678;
#endif
		CPPUNIT_ASSERT( statable->SetOwner(owner) == B_OK );
		CPPUNIT_ASSERT( statable->SetGroup(group) == B_OK );
		CPPUNIT_ASSERT( statable->SetPermissions(perms) == B_OK );
		CPPUNIT_ASSERT( statable->SetModificationTime(mtime) == B_OK );
		CPPUNIT_ASSERT( statable->SetCreationTime(ctime) == B_OK );
#if !TEST_R5 && !TEST_OBOS /* !!!POSIX ONLY!!! */
		CPPUNIT_ASSERT( statable->SetAccessTime(atime) == B_OK );
#endif
		CPPUNIT_ASSERT( lstat(entryName.c_str(), &st) == 0 );
		CPPUNIT_ASSERT( owner == st.st_uid );
		CPPUNIT_ASSERT( group == st.st_gid );
		CPPUNIT_ASSERT( perms == (st.st_mode & S_IUMSK) );
		CPPUNIT_ASSERT( mtime == st.st_mtime );
#ifndef __VOS__
		CPPUNIT_ASSERT( ctime == st.st_crtime );
#endif
#if !TEST_R5 && !TEST_OBOS /* !!!POSIX ONLY!!! */
		CPPUNIT_ASSERT( atime == st.st_atime );
#endif
	}
	testEntries.delete_all();
	// test with uninitialized objects
	NextSubTest();
	CreateUninitializedStatables(testEntries);
	for (testEntries.rewind(); testEntries.getNext(statable, entryName); ) {
		uid_t owner = 0xdad;
		gid_t group = 0xdee;
		mode_t perms = 0x0ab;	// -w- r-x -wx	-- unusual enough? ;-)
		time_t mtime = 1234567;
		time_t ctime = 654321;
		time_t atime = 2345678;
		CPPUNIT_ASSERT( statable->SetOwner(owner) != B_OK );
		CPPUNIT_ASSERT( statable->SetGroup(group) != B_OK );
		CPPUNIT_ASSERT( statable->SetPermissions(perms) != B_OK );
		CPPUNIT_ASSERT( statable->SetModificationTime(mtime) != B_OK );
		CPPUNIT_ASSERT( statable->SetCreationTime(ctime) != B_OK );
		CPPUNIT_ASSERT( statable->SetAccessTime(atime) != B_OK );
	}
	testEntries.delete_all();
}
