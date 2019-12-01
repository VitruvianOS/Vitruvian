AddOn(
	root
	SHARED

	LIBS
	rt
	dl
	pthread
	${ZLIB_LIBRARIES}
)

add_subdirectory(kernel_linux)
add_subdirectory(libroot)
