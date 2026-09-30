# The -dev packages ship the unversioned .so; catch a chroot built
# before they were added to its package list.
foreach(_lib ${CUPS_LIBRARIES} ${CAIRO_LIBRARIES})
	find_library(PRINTCUPS_${_lib}_LIBRARY NAMES ${_lib})
	if(NOT PRINTCUPS_${_lib}_LIBRARY)
		message(FATAL_ERROR "libprintcups: lib${_lib} not found in the "
			"build root; regenerate the chroot (bake --regenerate-chroot)")
	endif()
endforeach()

AddOn(
	printcups
	SHARED

	SOURCES
		CupsBridge.cpp
		CairoRenderer.cpp
		SetupPanels.cpp
		PrintCups.cpp

	LIBS
		${CUPS_LIBRARIES}
		${CAIRO_LIBRARIES}
		be
		root
		localestub
)

UsePrivateHeaders(printcups print interface shared support app)

target_link_options(printcups PRIVATE -Wl,--no-undefined)
