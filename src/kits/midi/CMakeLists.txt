AddOn(midi
	SHARED

	SOURCES
	Midi.cpp
	MidiPort.cpp
	MidiStore.cpp
	MidiSynth.cpp
	MidiSynthFile.cpp
	MidiText.cpp
	Samples.cpp
	Synth.cpp

	LIBS be root media2 midi2
)

UsePrivateHeaders(midi support media2 midi2)

target_include_directories(midi PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
