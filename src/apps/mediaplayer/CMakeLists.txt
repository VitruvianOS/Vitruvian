Application(MediaPlayer
	SOURCES
		CDMediaSource.cpp
		Controller.cpp
		ControllerView.cpp
		DurationToString.cpp
		MainWin.cpp
		MediaPlayerApp.cpp
		MidiPlayback.cpp
		Playlist.cpp
		PlaylistItem.cpp
		PlaylistWindow.cpp
		VideoView.cpp
		interface/DurationView.cpp
		interface/LocationStringView.cpp
		interface/PeakView.cpp
		interface/PlayPauseButton.cpp
		interface/PositionToolTip.cpp
		interface/SeekSlider.cpp
		interface/SymbolButton.cpp
		interface/TransportControlGroup.cpp
		interface/VolumeSlider.cpp
	LIBS be shared localestub media2 tracker
	RDEF MediaPlayer.rdef
)

target_include_directories(MediaPlayer PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/interface
)
UsePrivateHeaders(MediaPlayer interface)
DoCatalogs("x-vnd.Haiku-MediaPlayer" apps/mediaplayer)
