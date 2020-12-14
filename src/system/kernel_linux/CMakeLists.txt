add_subdirectory(messaging)

target_sources(root PRIVATE
	main.cpp

	Area.cpp
	byteorder.cpp
	debug.c
	framebuffer.cpp
	getenv.c
	image.cpp
	tls.cpp
	mutex.cpp
	port.c
	sem.c
	SystemWatcher.cpp
	Team.cpp
	Thread.cpp
	time.cpp
	wait_for_objects.cpp
	libc.c
	real_time_clock.c
	shutdown.cpp
	signal.cpp
	system_info.c
	system_ioctl.c

	fs/dir.c

	fs/fs2.cpp
	fs/fs_attr.cpp
	fs/fs_index.cpp
	fs/fs_info.cpp
	fs/fs_query.cpp
	fs/fs_volume.cpp
	fs/watch.cpp

	fs/disk_device/disk_device.cpp
)

target_include_directories(root PRIVATE
	"./"
)
