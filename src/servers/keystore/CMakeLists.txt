Server(
	keystore_server

	SOURCES
	AppAccessRequestWindow.cpp
	KeyRequestWindow.cpp
	Keyring.cpp
	KeyStoreServer.cpp
	KdbxFile.cpp
	VosCrypto.c
	KdbxCrypto.c

	INCLUDES
	LIBS localestub shared z crypto argon2
	RDEF keystore_server.rdef
)

UsePrivateHeaders(keystore_server app shared)
DoCatalogs("x-vnd.vos-keystore_server" servers/keystore)
