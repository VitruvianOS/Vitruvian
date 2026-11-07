Application(Media
	SOURCES
	Media.cpp
	MediaWindow.cpp
	DeviceListView.cpp
	DeviceMixerView.cpp
	StreamListView.cpp
	LevelMeterView.cpp
	SoundsSectionView.cpp
	ChannelStripView.cpp
	MixerSectionView.cpp
	HardwareSectionView.cpp
	AlsaControls.cpp
	HEventList.cpp
	SoundFilePanel.cpp
	LIBS be shared localestub media2 game tracker asound
	RDEF Media.rdef
)

target_include_directories(Media PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/src/kits/media2
)
UsePrivateHeaders(Media media2 interface preferences)
DoCatalogs("x-vnd.vos-Media" preferences/media)
