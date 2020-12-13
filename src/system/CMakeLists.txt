AddOn(
	root
	SHARED

	LIBS be_bootstrap rt dl pthread ${ZLIB_LIBRARIES}
)

# TODO: get rid of runtime loader
UsePrivateHeaders(root app shared libroot2 system kernel runtime_loader)

add_subdirectory(kernel_linux)
add_subdirectory(libroot)
