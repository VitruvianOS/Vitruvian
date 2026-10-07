AddOn(midi2
	SHARED

	SOURCES
	MidiConsumer.cpp
	MidiEndpoint.cpp
	MidiProducer.cpp
	MidiRoster.cpp
	MidiSchedule.cpp

	LIBS be root media2
)

UsePrivateHeaders(midi2 support media2)

target_include_directories(midi2 PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
