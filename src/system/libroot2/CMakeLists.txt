add_subdirectory(messaging)

target_sources(root PRIVATE
	main.cpp

	Area.cpp
	debug.cpp
	framebuffer.cpp
	getenv.c
	image.cpp
	tls.cpp
	mutex.cpp
	Port.cpp
	sem.c
	SystemWatcher.cpp
	Team.cpp
	Thread.cpp
	time.cpp
	wait_for_objects.cpp

	real_time_clock.c
	shutdown.cpp
	system_info.c


	compat/byteorder.cpp
	compat/libc.c
	compat/signal.cpp
	compat/system_ioctl.c

	fs/dir.c

	fs/find_directory.cpp
	fs/find_paths.cpp
	fs/fs2.cpp
	fs/fs_attr.cpp
	fs/fs_index.cpp
	fs/fs_info.cpp
	fs/fs_query.cpp
	fs/fs_volume.cpp
	fs/watch.cpp

	fs/disk_device/disk_device.cpp
	${BACKWARD_ENABLE}
)

target_include_directories(root PRIVATE
	"./"
)

add_backward(root)
