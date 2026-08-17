Application(Media
	SOURCES
	Media.cpp
	MediaWindow.cpp
	DeviceListView.cpp
	DeviceMixerView.cpp
	StreamListView.cpp
	LevelMeterView.cpp
	SoundsSectionView.cpp
	# Reused unchanged from the absorbed Sounds preflet (../sounds) — the
	# Sounds section is SoundsSectionView, a BGroupView port of its
	# HWindow; the event-list/file-panel logic below didn't need to change.
	${CMAKE_CURRENT_SOURCE_DIR}/../sounds/HEventList.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../sounds/SoundFilePanel.cpp
	LIBS be shared localestub media2 game tracker
	RDEF Media.rdef
)

target_include_directories(Media PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
UsePrivateHeaders(Media interface preferences)
DoCatalogs("x-vnd.Haiku-Media" preferences/media)
