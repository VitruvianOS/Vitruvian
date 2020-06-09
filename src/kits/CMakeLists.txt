AddOn(
	be
	SHARED

	LIBS root icon agg ${ZLIB_LIBRARIES} pthread icu localestub libshared
)

UsePrivateHeaders(be app interface kernel print shared locale)

AddOn(
	be_bootstrap
	STATIC

	LIBS pthread ${ZLIB_LIBRARIES}
)

UsePrivateHeaders(be_bootstrap interface app support)

add_subdirectory(app)
add_subdirectory(game)
add_subdirectory(interface)
add_subdirectory(locale)
#add_subdirectory(network)
add_subdirectory(shared)
add_subdirectory(storage)
add_subdirectory(support)
add_subdirectory(textencoding)
add_subdirectory(translation)
add_subdirectory(tracker)
