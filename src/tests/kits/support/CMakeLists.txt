UsePrivateHeaders(GLOBAL support)

Test(compression_test SOURCES compression_test.cpp)
Test(string_utf8_tests SOURCES string_utf8_tests.cpp)

add_definitions(-DNO_ELF_SYMBOL_PATCHING -DTEST_OBOS)

AddOn(
	supporttest
	SHARED

	SOURCES
	SupportKitTestAddon.cpp

	ByteOrderTest.cpp

	barchivable/ArchivableTest.cpp
	barchivable/BArchivableTester.cpp
	barchivable/FindInstantiationFuncTester.cpp
	barchivable/InstantiateObjectTester.cpp
	barchivable/LocalTestObject.cpp
	barchivable/ValidateInstantiationTester.cpp

	bautolock/AutolockTest.cpp
	bautolock/AutolockLockerTest.cpp
	bautolock/AutolockLooperTest.cpp

	DateTimeTest.cpp

	blocker/LockerTest.cpp
	blocker/BenaphoreLockCountTest1.cpp
	blocker/ConcurrencyTest1.cpp
	blocker/ConcurrencyTest2.cpp
	blocker/ConstructionTest1.cpp
	blocker/DestructionTest1.cpp
	blocker/DestructionTest2.cpp
	blocker/LockerTestCase.cpp
	blocker/SemaphoreLockCountTest1.cpp

	bmemoryio/MemoryIOTest.cpp
	bmemoryio/ConstTest.cpp
	bmemoryio/SeekTest.cpp
	bmemoryio/WriteTest.cpp
	bmemoryio/ReadTest.cpp
	bmemoryio/SetSizeTest.cpp

	bmemoryio/MallocIOTest.cpp
	bmemoryio/MallocSeekTest.cpp
	bmemoryio/MallocWriteTest.cpp
	bmemoryio/MallocBufferLengthTest.cpp

	bstring/StringTest.cpp
	bstring/StringConstructionTest.cpp
	bstring/StringAccessTest.cpp
	bstring/StringAssignTest.cpp
	bstring/StringAppendTest.cpp
	bstring/StringSubCopyTest.cpp
	bstring/StringPrependTest.cpp
	bstring/StringCaseTest.cpp
	bstring/StringInsertTest.cpp
	bstring/StringEscapeTest.cpp
	bstring/StringRemoveTest.cpp
	bstring/StringCompareTest.cpp
	bstring/StringFormatAppendTest.cpp
	bstring/StringCharAccessTest.cpp
	bstring/StringSearchTest.cpp
	bstring/StringReplaceTest.cpp
	bstring/StringSplitTest.cpp

	bblockcache/BlockCacheTest.cpp
	bblockcache/BlockCacheExerciseTest.cpp
	bblockcache/BlockCacheConcurrencyTest.cpp

	INCLUDES
	"${PROJECT_SOURCE_DIR}/headers/tools/cppunit/"
)
