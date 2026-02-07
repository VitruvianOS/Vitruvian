target_sources(root PRIVATE
	Team.cpp

	thread.cpp
	port.cpp
	sem.cpp
	area.cpp

	byteorder.cpp
	debug.cpp
	driver_settings.cpp
	extended_system_info.cpp
	framebuffer.cpp
	image.cpp
	parsedate.cpp
	real_time_clock.c
	shutdown.cpp
	system_info.c

	SystemWatcher.cpp
	KMessage.cpp

	fs/fs.cpp
	fs/vref.cpp
	fs/node_monitor.cpp
	fs/disk_device/disk_device.cpp
	fs/fs_info.cpp
	fs/mount.cpp
	fs/attr.cpp
	fs/index.cpp
	fs/query.cpp
	fs/find_directory.cpp
	fs/find_paths.cpp
	fs/normalize_path.cpp

	compat/Architecture.cpp
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

	compat/mutex.cpp
	compat/locks/init_once.cpp
	compat/locks/mutex.cpp
	compat/locks/recursive_lock.cpp
)

add_dependencies(root nexus)

target_include_directories(root PRIVATE
	"./"
)
