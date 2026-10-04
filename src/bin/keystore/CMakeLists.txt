Application(keystore
	SOURCES
	keystore.cpp

	INCLUDES
	${CMAKE_SOURCE_DIR}/src/libs/kdbx

	LIBS kdbx

	RDEF keystore.rdef
)
