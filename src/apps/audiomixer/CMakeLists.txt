RunnableAddOn(AudioMixer
	SOURCES
	AudioMixerApp.cpp
	AudioMixerStatus.cpp
	AudioMixerView.cpp
	VolumePopupView.cpp
	LIBS be shared localestub media2
	RDEF AudioMixer.rdef SpeakerIcons.rdef
)

target_include_directories(AudioMixer PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
UsePrivateHeaders(AudioMixer interface shared preferences)
DoCatalogs("x-vnd.Haiku-AudioMixer" apps/audiomixer)
