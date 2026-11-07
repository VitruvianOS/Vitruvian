AddOn(
	vos_x_client
	SHARED

	SOURCES
		XClientAttach.cpp

	LIBS
		be
		root
)

target_link_options(vos_x_client PRIVATE -Wl,--no-undefined)
