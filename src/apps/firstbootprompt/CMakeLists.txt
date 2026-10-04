Application(FirstBootPrompt
	SOURCES
	BootPrompt.cpp
	BootPromptWindow.cpp
	${PROJECT_SOURCE_DIR}/src/preferences/keymap/Keymap.cpp
	LIBS shared translation localestub
	RDEF BootPrompt.rdef
)
UsePrivateHeaders(FirstBootPrompt shared locale app interface system kernel)
target_include_directories(FirstBootPrompt PRIVATE
	${PROJECT_SOURCE_DIR}/src/preferences/keymap)

DoCatalogs("x-vnd.vos-FirstBootPrompt" apps/firstbootprompt)
