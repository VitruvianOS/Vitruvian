
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

	${LIBROOT_SOURCES}/driver_settings.cpp
	${LIBROOT_SOURCES}/framebuffer.cpp
	${LIBROOT_SOURCES}/mutex.cpp
	${LIBROOT_SOURCES}/locks/mutex.cpp
	${LIBROOT_SOURCES}/Architecture.cpp
	${LIBROOT_SOURCES}/compat/time.cpp

	${LIBROOT_SOURCES}/messaging/KMessage.cpp

	${LIBROOT_SOURCES}/real_time_clock.c
	${LIBROOT_SOURCES}/shutdown.cpp
	${LIBROOT_SOURCES}/system_info.c

	${LIBROOT_SOURCES}/byteorder.cpp
	${LIBROOT_SOURCES}/compat/libc.c
	${LIBROOT_SOURCES}/compat/getenv.c

	${LIBROOT_SOURCES}/fs/normalize_path.cpp
	${LIBROOT_SOURCES}/fs/find_directory.cpp
	${LIBROOT_SOURCES}/fs/find_paths.cpp
	${LIBROOT_SOURCES}/fs/fs_attr.cpp
	${LIBROOT_SOURCES}/fs/fs_index.cpp
	${LIBROOT_SOURCES}/fs/fs_info.cpp
	${LIBROOT_SOURCES}/fs/fs_query.cpp
	${LIBROOT_SOURCES}/fs/fs_volume.c
	${LIBROOT_SOURCES}/fs/watch.cpp

	${LIBROOT_SOURCES}/fs/disk_device/disk_device.cpp

	${LIBROOT_SOURCES}/locks/init_once.cpp
	${LIBROOT_SOURCES}/locks/recursive_lock.cpp

	LIBS rt dl pthread ${ZLIB_LIBRARIES}
)

target_include_directories(root_build PRIVATE ${LIBROOT_SOURCES})

UsePrivateHeaders(root_build app shared libroot system kernel)
