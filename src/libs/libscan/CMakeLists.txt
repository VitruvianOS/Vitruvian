# The -dev package ships the unversioned .so; catch a chroot built
# before SANE was added to its package list.
find_library(SCAN_sane_LIBRARY NAMES sane)
if(NOT SCAN_sane_LIBRARY)
	message(FATAL_ERROR "libscan: libsane not found in the "
		"build root; regenerate the chroot (bake --regenerate-chroot)")
endif()

AddOn(
	libscan
	SHARED

	SOURCES
		LibScan.cpp

	LIBS
		${SANE_LIBRARIES}
		be
		root
		localestub
)

UsePrivateHeaders(libscan scan interface support)

target_link_options(libscan PRIVATE -Wl,--no-undefined)
