Server(
	mount_server

	SOURCES
	AutoMounter.cpp

	INCLUDES
	LIBS tracker localestub
	RDEF mount_server.rdef
)

UsePrivateHeaders(mount_server app mount shared storage system)
DoCatalogs("x-vnd.vos-mount_server" servers/mount)
