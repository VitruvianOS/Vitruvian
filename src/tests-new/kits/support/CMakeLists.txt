# Support Kit unit tests
UnitTest(support-kit-tests
	SOURCES
	ArchivableTest.cpp
	ArchivableTestObject.cpp
	AutolockTest.cpp
	BlockCacheConcurrencyTest.cpp
	BlockCacheTest.cpp
	ByteOrderTest.cpp
	DateTimeTest.cpp
	FindInstantiationFuncTest.cpp
	InstantiateObjectTest.cpp
	LockerConcurrencyTest.cpp
	LockerConstructionTest.cpp
	LockerCountTest.cpp
	LockerDestructionTest.cpp
	MallocIOTest.cpp
	MemoryIOTest.cpp
	ValidateInstantiationTest.cpp

	StringAccessTest.cpp
	StringAppendTest.cpp
	StringAssignTest.cpp
	StringCaseTest.cpp
	StringCharAccessTest.cpp
	StringCharsTest.cpp
	StringCompareTest.cpp
	StringConstructionTest.cpp
	StringEscapeTest.cpp
	StringFormatAppendTest.cpp
	StringInsertTest.cpp
	StringPrependTest.cpp
	StringRemoveTest.cpp
	StringReplaceTest.cpp
	StringSearchTest.cpp
	StringSplitTest.cpp
	StringSubCopyTest.cpp
)

# find_instantiation_func() searches the symbols of every loaded image. Unlike
# Haiku's runtime loader, the host linker only makes an executable's symbols
# visible when it is linked with -rdynamic, which is what ENABLE_EXPORTS does.
set_target_properties(support-kit-tests PROPERTIES ENABLE_EXPORTS TRUE)

# An add-on holding an archivable class, so the instantiate_object() tests can
# look a class up in an image other than their own. The rdef gives it the MIME
# signature the tests resolve through BRoster.
add_library(RemoteTestObject MODULE RemoteTestObject.cpp)
target_compile_options(RemoteTestObject PRIVATE -include LinuxBuildCompatibility.h)
target_link_libraries(RemoteTestObject PRIVATE be root)
CompileRdef(RemoteTestObject RemoteTestObject.rdef)
LinkRdefs(RemoteTestObject)

add_dependencies(support-kit-tests RemoteTestObject)
target_compile_definitions(support-kit-tests PRIVATE
	REMOTE_ADDON_PATH="$<TARGET_FILE:RemoteTestObject>")
