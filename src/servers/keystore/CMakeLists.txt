Server(
	keystore_server

	SOURCES
	AppAccessRequestWindow.cpp
	KeyRequestWindow.cpp
	Keyring.cpp
	KeyStoreServer.cpp
	SecretServiceBridge.cpp
	SecretServiceMap.c
	VosCrypto.c

	INCLUDES
	LIBS localestub shared crypto
	RDEF keystore_server.rdef
)

UsePrivateHeaders(keystore_server app shared)
DoCatalogs("x-vnd.vos-keystore_server" servers/keystore)

find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
	pkg_check_modules(GIO REQUIRED gio-2.0)
	target_include_directories(keystore_server PRIVATE ${GIO_INCLUDE_DIRS})
	target_link_libraries(keystore_server PRIVATE ${GIO_LIBRARIES})
	target_compile_options(keystore_server PRIVATE ${GIO_CFLAGS_OTHER})
endif()
