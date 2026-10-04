Application(keystore
	SOURCES
	keystore.cpp
	${CMAKE_SOURCE_DIR}/src/servers/keystore/KdbxFile.cpp
	${CMAKE_SOURCE_DIR}/src/servers/keystore/VosCrypto.c
	${CMAKE_SOURCE_DIR}/src/servers/keystore/KdbxCrypto.c

	INCLUDES
	${CMAKE_SOURCE_DIR}/src/servers/keystore

	LIBS z crypto argon2

	RDEF keystore.rdef
)
