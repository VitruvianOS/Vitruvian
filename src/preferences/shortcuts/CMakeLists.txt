Application(Shortcuts
	SOURCES
	EditWindow.cpp
	main.cpp
	MetaKeyStateMap.cpp
	PopUpColumn.cpp
	ShortcutsApp.cpp
	ShortcutsSpec.cpp
	ShortcutsWindow.cpp
	${PROJECT_SOURCE_DIR}/src/add-ons/input_server/filters/shortcut_catcher/BitFieldTesters.cpp
	${PROJECT_SOURCE_DIR}/src/add-ons/input_server/filters/shortcut_catcher/CommandActuators.cpp
	${PROJECT_SOURCE_DIR}/src/add-ons/input_server/filters/shortcut_catcher/KeyInfos.cpp
	${PROJECT_SOURCE_DIR}/src/add-ons/input_server/filters/shortcut_catcher/ParseCommandLine.cpp
	LIBS tracker localestub
	RDEF Shortcuts.rdef
)
target_include_directories(Shortcuts PRIVATE
	${PROJECT_SOURCE_DIR}/src/add-ons/input_server/filters/shortcut_catcher)
