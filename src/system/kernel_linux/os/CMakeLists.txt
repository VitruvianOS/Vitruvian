target_sources(root PRIVATE
	byteorder.cpp
	framebuffer.cpp
	area.c
	atomic.c
	debug.c
	getenv.c
	image.c
	memory.cpp
	missing_imp.c
	mutex.cpp
	port.c
	scheduler.cpp
	sem.c
	team.c
	thread.c
	time.cpp
	wait_for_objects.cpp
	watch.c
	libc.c
	real_time_clock.c
	shutdown.cpp
	system.c
	system_ioctl.c

	fs/build.cpp
	fs/dir.c
	#fs/fs_attr.cpp
	#fs/fs_attr_generic.cpp
	fs/fs_attr_untyped.cpp
	fs/fs.cpp
	fs/fs_descriptors.cpp
	fs/fs_impl.h
	fs/fs_index.c
	fs/fs_info.cpp
	fs/fs_query.c
	fs/fs_volume.c
	#fs/kernel_interface.POSIX.cpp
	fs/volume/LinuxVolume.cpp

	locks/lock.cpp

	tls.cpp
)
