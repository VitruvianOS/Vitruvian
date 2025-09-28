
set (LIBROOT_SOURCES ${CMAKE_SOURCE_DIR}/src/system/libroot2/)

AddOn(
	root_build
	SHARED

	SOURCES
	sem.cpp
	thread.cpp
	port.cpp
	main.cpp
	errors.cpp
	debug.cpp

	#Area.cpp
	#${LIBROOT_SOURCES}/debug.cpp
	${LIBROOT_SOURCES}/driver_settings.cpp
	#${LIBROOT_SOURCES}/extended_system_info.cpp
	${LIBROOT_SOURCES}/framebuffer.cpp
	${LIBROOT_SOURCES}/image.cpp
	${LIBROOT_SOURCES}/mutex.cpp
	${LIBROOT_SOURCES}/locks/mutex.cpp
	${LIBROOT_SOURCES}/Architecture.cpp
	#${LIBROOT_SOURCES}/parsedate.cpp
	#Port.cpp
	#sem.c
	#SystemWatcher.cpp
	#${LIBROOT_SOURCES}/Team.cpp
	#Thread.cpp
	${LIBROOT_SOURCES}/compat/time.cpp

	${LIBROOT_SOURCES}/messaging/KMessage.cpp

	${LIBROOT_SOURCES}/real_time_clock.c
	${LIBROOT_SOURCES}/shutdown.cpp
	${LIBROOT_SOURCES}/system_info.c

	${LIBROOT_SOURCES}/compat/byteorder.cpp
	${LIBROOT_SOURCES}/compat/libc.c
	#${LIBROOT_SOURCES}/compat/scheduler.c
	#${LIBROOT_SOURCES}/compat/signal.cpp
	#${LIBROOT_SOURCES}/compat/system_info.cpp
	#${LIBROOT_SOURCES}/compat/system_revision.c
	#${LIBROOT_SOURCES}/compat/system_ioctl.c
	#${LIBROOT_SOURCES}/compat/tls.cpp
	#${LIBROOT_SOURCES}/compat/wait_for_objects.cpp

	${LIBROOT_SOURCES}/fs/find_directory.cpp
	${LIBROOT_SOURCES}/fs/find_paths.cpp
	${LIBROOT_SOURCES}/fs/fs3.cpp
	${LIBROOT_SOURCES}/fs/fs_attr.cpp
	${LIBROOT_SOURCES}/fs/fs_index.cpp
	${LIBROOT_SOURCES}/fs/fs_info.cpp
	${LIBROOT_SOURCES}/fs/fs_query.cpp
	${LIBROOT_SOURCES}/fs/fs_volume.c
	${LIBROOT_SOURCES}/fs/watch.cpp
	${LIBROOT_SOURCES}/fs/getenv.c

	${LIBROOT_SOURCES}/fs/disk_device/disk_device.cpp

	${LIBROOT_SOURCES}/locks/init_once.cpp
	#${LIBROOT_SOURCES}/locks/mutex.cpp
	${LIBROOT_SOURCES}/locks/recursive_lock.cpp
	LIBS rt dl pthread ${ZLIB_LIBRARIES}
)

target_include_directories(root_build PRIVATE ${LIBROOT_SOURCES})

UsePrivateHeaders(root_build app shared libroot system kernel)
