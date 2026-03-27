Application(desklink
	SOURCES
	desklink.cpp
	DeskButton.cpp
	MediaReplicant.cpp
	MixerControl.cpp
	VolumeControl.cpp
	VolumeWindow.cpp

	LIBS media localestub
	RDEF desklink.rdef SpeakerBox.rdef
)

UsePrivateHeaders(desklink app interface shared)
