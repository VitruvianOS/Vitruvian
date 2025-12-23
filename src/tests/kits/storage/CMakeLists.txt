UsePrivateHeaders(GLOBAL storage)

add_subdirectory(testapps)
add_subdirectory(virtualdrive)

add_definitions(-DNO_ELF_SYMBOL_PATCHING -DTEST_OBOS)

AddOn(
	storagetest
	SHARED

	SOURCES
	StorageKitTestAddon.cpp

	AppFileInfoTest.cpp
	BasicTest.cpp
	DirectoryTest.cpp
	EntryTest.cpp
	FindDirectoryTest.cpp
	FileTest.cpp
	MimeSnifferTest.cpp
	NodeInfoTest.cpp
	NodeTest.cpp
	PathTest.cpp
	QueryTest.cpp
	ResourcesTest.cpp
	ResourceStringsTest.cpp
	StatableTest.cpp
	SymLinkTest.cpp
	TestApp.cpp
	VolumeTest.cpp

	#MimeTypeTest.cpp

	INCLUDES
	"${PROJECT_SOURCE_DIR}/headers/tools/cppunit/"
)

UsePrivateHeaders(storagetest storage)
