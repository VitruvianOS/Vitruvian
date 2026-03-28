Application(FirstBootPrompt
	SOURCES
	BootPrompt.cpp
	BootPromptWindow.cpp
	${PROJECT_SOURCE_DIR}/src/preferences/keymap/Keymap.cpp
	${PROJECT_SOURCE_DIR}/src/preferences/keymap/KeymapListItem.cpp
	LIBS shared translation localestub
	RDEF BootPrompt.rdef
)
UsePrivateHeaders(FirstBootPrompt shared locale app interface system)
target_include_directories(FirstBootPrompt PRIVATE
	${PROJECT_SOURCE_DIR}/src/preferences/keymap)
