
set (LIBROOT_SOURCES ${CMAKE_SOURCE_DIR}/src/system/libroot2/)

AddOn(
	root_build
	STATIC

	SOURCES
	sem.cpp
	thread.cpp
	port.cpp
	main.cpp
	errors.cpp
	debug.cpp
	image.cpp
	fs3.cpp

	${LIBROOT_SOURCES}/KMessage.cpp
	${LIBROOT_SOURCES}/real_time_clock.c
	${LIBROOT_SOURCES}/system_info.c
	${LIBROOT_SOURCES}/byteorder.cpp
	${LIBROOT_SOURCES}/fs/normalize_path.cpp
	${LIBROOT_SOURCES}/fs/find_directory.cpp
	${LIBROOT_SOURCES}/fs/attr.cpp

	LIBS rt dl pthread ${ZLIB_LIBRARIES}
)

target_include_directories(root_build PRIVATE ${LIBROOT_SOURCES} kernel)

UsePrivateHeaders(root_build app shared libroot system kernel)
