Application(
	NetPlus

	SOURCES
	main.cpp
	DownloadsWindow.cpp
	MainWindow.cpp
	SettingsWindow.cpp
	PanelMenu.cpp
	PanelMenuModel.cpp
	StatusBarView.cpp
	ToolbarIcons.cpp
	ToolbarView.cpp

	LIBS web tracker

	RDEF NetPlus.rdef
)

target_include_directories(NetPlus PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${PROJECT_SOURCE_DIR}/headers
	${PROJECT_SOURCE_DIR}/headers/os
)

# std::filesystem for the profile paths.
set_target_properties(NetPlus PROPERTIES
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED ON
)
