AddOn(
	root
	SHARED

	LIBS be_bootstrap rt dl pthread ${ZLIB_LIBRARIES}
)

add_subdirectory(kernel_linux)
add_subdirectory(libroot)
