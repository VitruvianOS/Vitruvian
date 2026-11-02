Application(MediaPlayer
	SOURCES
		CDMediaSource.cpp
		Controller.cpp
		ControllerView.cpp
		DurationToString.cpp
		FullScreenState.cpp
		InfoWin.cpp
		MainWin.cpp
		MediaPlayerApp.cpp
		MidiPlayback.cpp
		Playlist.cpp
		PlaylistItem.cpp
		PlaylistWindow.cpp
		VideoDisplay.cpp
		PlayerVideoView.cpp
		interface/DurationView.cpp
		interface/LocationStringView.cpp
		interface/PeakView.cpp
		interface/PlayPauseButton.cpp
		interface/PositionToolTip.cpp
		interface/SeekSlider.cpp
		interface/SymbolButton.cpp
		interface/TransportControlGroup.cpp
		interface/VolumeSlider.cpp
	LIBS be shared localestub media2 midi tracker
	RDEF MediaPlayer.rdef
)

target_include_directories(MediaPlayer PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/interface
	${CMAKE_SOURCE_DIR}/src/kits/media2
)
UsePrivateHeaders(MediaPlayer interface shared)
DoCatalogs("x-vnd.vos-MediaPlayer" apps/mediaplayer)
