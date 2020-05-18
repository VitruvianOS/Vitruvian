AddOn(
	root
	SHARED

	LIBS be_bootstrap rt dl pthread ${ZLIB_LIBRARIES}
)

UsePrivateHeaders(root app kernel shared system libroot runtime_loader)

add_subdirectory(kernel_linux)
add_subdirectory(libroot)
