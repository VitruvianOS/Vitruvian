Application(LaunchBox
	SOURCES
	App.cpp
	LaunchButton.cpp
	main.cpp
	MainWindow.cpp
	NamePanel.cpp
	PadView.cpp
	Panel.cpp
	support.cpp
	LIBS shared translation localestub
	RDEF LaunchBox.rdef
)
UsePrivateHeaders(LaunchBox shared)
