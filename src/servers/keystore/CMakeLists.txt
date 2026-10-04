Server(
	keystore_server

	SOURCES
	AppAccessRequestWindow.cpp
	KeyRequestWindow.cpp
	Keyring.cpp
	KeyStoreServer.cpp

	INCLUDES
	LIBS localestub shared
	RDEF keystore_server.rdef
)

UsePrivateHeaders(keystore_server app shared)
DoCatalogs("x-vnd.vos-keystore_server" servers/keystore)
