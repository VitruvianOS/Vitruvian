Application(Sounds
	SOURCES
	HApp.cpp
	HEventList.cpp
	HWindow.cpp
	SoundFilePanel.cpp
	LIBS media2 game tracker localestub
	RDEF Sounds.rdef
)

target_include_directories(Sounds PRIVATE ${CMAKE_SOURCE_DIR}/src/kits/media2)
