Application(Passwords
	SOURCES
	Passwords.cpp
	PasswordsView.cpp
	PasswordDialog.cpp
	EntryWindow.cpp
	${CMAKE_SOURCE_DIR}/src/servers/keystore/VosCrypto.c

	INCLUDES
	${CMAKE_SOURCE_DIR}/src/servers/keystore
	${CMAKE_SOURCE_DIR}/src/libs/kdbx

	LIBS localestub tracker kdbx crypto

	RDEF Passwords.rdef
)

UsePrivateHeaders(Passwords interface shared locale app)
DoCatalogs("x-vnd.vos-Passwords" apps/passwords)
