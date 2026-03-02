UsePrivateHeaders(GLOBAL shared)

add_definitions(-DNO_ELF_SYMBOL_PATCHING -DTEST_OBOS)

AddOn(
	sharedtest
	SHARED

	SOURCES
	SharedTestAddon.cpp

	CalendarViewTest.cpp
	ChecksumJsonEventListener.cpp
	#DriverSettingsMessageAdapterTest.cpp
	FakeJsonDataGenerator.cpp
	JsonEndToEndTest.cpp
	JsonErrorHandlingTest.cpp
	JsonTextWriterTest.cpp
	JsonToMessageTest.cpp
	KeymapTest.cpp
	LRUCacheTest.cpp
	MemoryRingIOTest.cpp
	NaturalCompareTest.cpp

	INCLUDES
	"${PROJECT_SOURCE_DIR}/headers/tools/cppunit/"
)
