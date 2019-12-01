AddOn(
	be
	SHARED

	LIBS pthread root linprog icon agg icu ${ZLIB_LIBRARIES}
)

AddOn(
	be_bootstrap
	STATIC

	INCLUDES
	"../../headers/private/interface/"

	LIBS pthread ${ZLIB_LIBRARIES}
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
