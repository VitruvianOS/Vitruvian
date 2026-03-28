Application(Backgrounds
	SOURCES
	BackgroundImage.cpp
	Backgrounds.cpp
	BackgroundsView.cpp
	ImageFilePanel.cpp
	LIBS tracker translation localestub
	RDEF Backgrounds.rdef
)
target_include_directories(Backgrounds PRIVATE
	${PROJECT_SOURCE_DIR}/headers/private/preferences)
