Application(media_client
	SOURCES
	media_client.cpp
	MediaPlay.cpp
	MediaTest.cpp
	MidiPlay.cpp
	MediaUnits.cpp
	${CMAKE_SOURCE_DIR}/src/apps/cortex/CortexMediaUnits.cpp
	LIBS media2 midi midi2
)

target_include_directories(media_client PRIVATE
	${CMAKE_SOURCE_DIR}/src/apps/cortex
	${GSTREAMER_INCLUDES}
)

target_link_libraries(media_client PRIVATE
	${GSTREAMER_LIBRARIES}
)
