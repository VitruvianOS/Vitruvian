Application(
	NetPlus

	SOURCES
	main.cpp
	MainWindow.cpp
	SettingsWindow.cpp
	DeskbarEntry.cpp
	DownloadsModel.cpp
	DownloadsPanel.cpp
	HistoryStore.cpp
	InstalledAppStore.cpp
	PanelMenu.cpp
	PanelMenuModel.cpp
	StatusBarView.cpp
	SuggestModel.cpp
	SuggestPopup.cpp
	ToolbarIcons.cpp
	ToolbarView.cpp
	UrlBarView.cpp
	UrlInput.cpp
	WebAppManifest.cpp

	LIBS web tracker translation

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
