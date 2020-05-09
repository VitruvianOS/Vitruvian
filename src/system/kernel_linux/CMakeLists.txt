add_subdirectory(messaging)

target_sources(root PRIVATE
	main.cpp

	area.c
	byteorder.cpp
	debug.c
	extended_team_info.cpp
	framebuffer.cpp
	getenv.c
	image.cpp
	tls.cpp
	memory.cpp
	mutex.cpp
	port.c
	scheduler.cpp
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
	fs/fs_attr_untyped.cpp
	fs/fs2.cpp
	fs/fs_descriptors.cpp
	fs/fs_impl.h
	fs/fs_index.c
	fs/fs_info.cpp
	fs/fs_query.c
	fs/fs_volume.c
	fs/missing_imp.c
	fs/watch.c
	#fs/kernel_interface.POSIX.cpp
	fs/volume/LinuxVolume.cpp

	fs/disk_device/disk_device.cpp
)

target_include_directories(root PRIVATE
	"./"
)
