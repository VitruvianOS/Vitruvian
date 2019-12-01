AddOn(
	be
	SHARED

	LIBS
	root
	linprog
	icon
	agg
	icu
	${ZLIB_LIBRARIES}
)

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
