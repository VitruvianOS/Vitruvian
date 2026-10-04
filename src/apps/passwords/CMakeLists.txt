Application(Passwords
	SOURCES
	Passwords.cpp
	PasswordsView.cpp
	${CMAKE_SOURCE_DIR}/src/servers/keystore/KdbxFile.cpp
	${CMAKE_SOURCE_DIR}/src/servers/keystore/VosCrypto.c
	${CMAKE_SOURCE_DIR}/src/servers/keystore/KdbxCrypto.c

	INCLUDES
	${CMAKE_SOURCE_DIR}/src/servers/keystore

	LIBS localestub tracker z crypto argon2

	RDEF Passwords.rdef
)

UsePrivateHeaders(Passwords interface shared locale app)
DoCatalogs("x-vnd.Vitruvian-Passwords" apps/passwords)
