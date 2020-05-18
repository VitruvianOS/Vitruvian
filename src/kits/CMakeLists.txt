AddOn(
	be
	SHARED

	LIBS pthread root linprog icon agg ${ZLIB_LIBRARIES}
)

target_link_libraries(be PUBLIC icu)

UsePrivateHeaders( be app interface kernel locale print shared )

AddOn(
	be_bootstrap
	STATIC

	LIBS pthread ${ZLIB_LIBRARIES}
)

UsePrivateHeaders( be_bootstrap interface app support )

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
