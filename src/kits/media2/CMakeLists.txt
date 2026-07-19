AddOn(media2
	SHARED

	SOURCES
	Controllable.cpp
	DefaultMediaTheme.cpp
	FormatConversion.cpp
	MediaDefs.cpp
	GStreamerBackend.cpp
	MediaClient.cpp
	MediaConnection.cpp
	MediaFile.cpp
	MediaFiles.cpp
	MediaFormat.cpp
	MediaFormats.cpp
	MediaGraph.cpp
	MediaNode.cpp
	MediaAutomation.cpp
	MediaPlayer.cpp
	MediaRecorder.cpp
	MediaTheme.cpp
	ParameterWeb.cpp
	PipeWireBackend.cpp
	PlaySound.cpp
	SimpleMediaClient.cpp
	SimpleMediaNode.cpp
	Sound.cpp
	SoundPlayer.cpp
	TimeCode.cpp

	LIBS be root
)

UsePrivateHeaders(media2 app support storage)

# PipeWire / SPA / GStreamer headers and libraries
target_include_directories(media2 PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/headers/build/private/kernel
	${PIPEWIRE_INCLUDES}
	${SPA_INCLUDES}
	${GSTREAMER_INCLUDES}
)

target_link_libraries(media2 PRIVATE
	${PIPEWIRE_LIBRARIES}
	${GSTREAMER_LIBRARIES}
)
