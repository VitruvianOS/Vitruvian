AddOn(media2
	SHARED

	SOURCES
	Controllable.cpp
	DefaultMediaTheme.cpp
	FormatConversion.cpp
	MediaDefs.cpp
	GStreamerBackend.cpp
	CameraSettings.cpp
	CameraSource.cpp
	MediaClient.cpp
	MediaConnection.cpp
	MediaDecoder.cpp
	MediaEncoder.cpp
	MediaFile.cpp
	MediaFiles.cpp
	MediaFormat.cpp
	MediaFormats.cpp
	MediaGraph.cpp
	RoutingStore.cpp
	MediaNode.cpp
	MediaUnit.cpp
	MidiUnit.cpp
	FilterUnitCore.cpp
	MediaFilter.cpp
	VideoFilters.cpp
	AudioFilters.cpp
	FilterCatalog.cpp
	MediaAutomation.cpp
	MediaEncodeTables.cpp
	MediaPlayer.cpp
	MediaRecorder.cpp
	MediaTheme.cpp
	MediaSynth.cpp
	ParameterWeb.cpp
	PeakMeter.cpp
	PipeWireBackend.cpp
	PlaySound.cpp
	SimpleMediaNode.cpp
	SimpleMediaUnit.cpp
	Sound.cpp
	SoundFontSynth.cpp
	SoundPlayer.cpp
	SystemBeep.cpp
	TimeCode.cpp

	LIBS be root shared
)

UsePrivateHeaders(media2 media2 app support storage shared)

# PipeWire / SPA / GStreamer / fluidsynth headers and libraries
target_include_directories(media2 PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/headers/build/private/kernel
	${PIPEWIRE_INCLUDES}
	${SPA_INCLUDES}
	${GSTREAMER_INCLUDES}
	${FLUIDSYNTH_INCLUDES}
)

target_link_libraries(media2 PRIVATE
	${PIPEWIRE_LIBRARIES}
	${GSTREAMER_LIBRARIES}
	${FLUIDSYNTH_LIBRARIES}
)
