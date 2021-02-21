target_sources(root PRIVATE
	Architecture.cpp
	driver_settings.cpp
	extended_system_info.cpp
	fs_attr.cpp
	fs_index.c
	fs_info.c
	fs_query.cpp
	fs_volume.c
	parsedate.cpp
	port.c
	scheduler.c
	sem.c
	system_info.cpp
	system_revision.c

	locks/init_once.cpp
	locks/mutex.cpp
	locks/recursive_lock.cpp
)
