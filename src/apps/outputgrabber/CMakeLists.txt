Application(
	outputgrabber

	SOURCES
		OutputGrabber.cpp

	LIBS
		outputaddon

	INCLUDES
		${CMAKE_SOURCE_DIR}/headers/private/interface
)

UsePrivateHeaders(outputgrabber interface support)
