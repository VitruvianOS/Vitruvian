Application(
	NetPlus

	SOURCES
	main.cpp
	MainWindow.cpp
	StatusBarView.cpp
	ToolbarView.cpp

	LIBS web

	RDEF NetPlus.rdef
)

target_include_directories(NetPlus PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${PROJECT_SOURCE_DIR}/headers
	${PROJECT_SOURCE_DIR}/headers/os
)
