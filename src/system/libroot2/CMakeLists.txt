add_subdirectory(messaging)

target_sources(root PRIVATE
	area.cpp
	byteorder.cpp
	Architecture.cpp
	debug.cpp
	driver_settings.cpp
	extended_system_info.cpp
	framebuffer.cpp
	image.cpp
	mutex.cpp
	parsedate.cpp
	Port.cpp
	sem.cpp
	SystemWatcher.cpp
	Team.cpp
	Thread.cpp
	vref.cpp

	real_time_clock.c
	shutdown.cpp
	system_info.c

	compat/libc.c
	compat/scheduler.c
	compat/signal.cpp
	compat/system_info.cpp
	compat/system_revision.c
	compat/system_ioctl.c
	compat/time.cpp
	compat/tls.cpp
	compat/wait_for_objects.cpp
	compat/getenv.c

	fs/find_directory.cpp
	fs/find_paths.cpp
	fs/fs4.cpp
	fs/fs_attr.cpp
	fs/fs_index.cpp
	fs/fs_info.cpp
	fs/fs_query.cpp
	fs/fs_volume.c
	fs/getenv.c
	fs/normalize_path.cpp
	fs/watch.cpp

	fs/disk_device/disk_device.cpp

	locks/init_once.cpp
	locks/mutex.cpp
	locks/recursive_lock.cpp
)

add_dependencies(root nexus)

target_include_directories(root PRIVATE
	"./"
)
