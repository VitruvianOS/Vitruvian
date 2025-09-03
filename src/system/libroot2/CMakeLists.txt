add_subdirectory(messaging)

target_sources(root PRIVATE
	Area.cpp
	Architecture.cpp
	debug.cpp
	driver_settings.cpp
	extended_system_info.cpp
	framebuffer.cpp
	image.cpp
	mutex.cpp
	parsedate.cpp
	Port.cpp
	sem.c
	SystemWatcher.cpp
	Team.cpp
	Thread.cpp
	time.cpp

	real_time_clock.c
	shutdown.cpp
	system_info.c

	compat/byteorder.cpp
	compat/libc.c
	compat/scheduler.c
	compat/signal.cpp
	compat/system_info.cpp
	compat/system_revision.c
	compat/system_ioctl.c
	compat/tls.cpp
	compat/wait_for_objects.cpp

	fs/find_directory.cpp
	fs/find_paths.cpp
	fs/fs3.cpp
	fs/fs_attr.cpp
	fs/fs_index.cpp
	fs/fs_info.cpp
	fs/fs_query.cpp
	fs/fs_volume.c
	fs/watch.cpp
	fs/getenv.c

	fs/disk_device/disk_device.cpp

	locks/init_once.cpp
	locks/mutex.cpp
	locks/recursive_lock.cpp
)

add_dependencies(root nexus)

target_include_directories(root PRIVATE
	"./"
)