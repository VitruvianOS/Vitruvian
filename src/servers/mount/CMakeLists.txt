Server(
	mount_server

	SOURCES
	AutoMounter.cpp

	INCLUDES
	LIBS tracker localestub
	RDEF mount_server.rdef
)

UsePrivateHeaders(mount_server app mount shared storage)
