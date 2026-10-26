AddOn(
	prefs
	SHARED

	SOURCES
		libprefs.cpp

	LIBS
		be
		root
)

target_link_options(prefs PRIVATE -Wl,--no-undefined)
