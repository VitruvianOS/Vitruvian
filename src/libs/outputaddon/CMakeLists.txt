AddOn(
	outputaddon
	SHARED

	SOURCES
		OutputAddonClient.cpp

	LIBS
		be
		root
)

UsePrivateHeaders(outputaddon interface support app)
