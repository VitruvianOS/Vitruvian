add_subdirectory(messaging)

target_sources(root PRIVATE
	main.cpp

	area.c
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
	timer.cpp
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
	fs/fs_index.c
	fs/fs_info.cpp
	fs/fs_query.c
	fs/fs_volume.c
	fs/watch.c
	fs/volume/LinuxVolume.cpp

	fs/disk_device/disk_device.cpp
)

target_include_directories(root PRIVATE
	"./"
)
