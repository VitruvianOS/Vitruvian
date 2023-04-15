
set (KITS_SOURCES ${CMAKE_SOURCE_DIR}/src/kits/)

AddOn(
	be_build
	STATIC

	LIBS pthread ${ZLIB_LIBRARIES} root_build
)

UsePrivateBuildHeaders(be_build app interface kernel support shared libroot system storage drivers)

add_subdirectory(app)
add_subdirectory(interface)
add_subdirectory(storage)
add_subdirectory(support)
