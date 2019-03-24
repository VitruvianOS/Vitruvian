add_subdirectory(os)
add_subdirectory(posix)

target_sources(root PRIVATE
	libroot_init.c
)
