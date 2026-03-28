Application(Switcher
	SOURCES
	ApplicationsView.cpp
	CaptureWindow.cpp
	GroupListView.cpp
	LaunchButton.cpp
	PanelWindow.cpp
	Switcher.cpp
	WindowsView.cpp
	LIBS shared translation localestub
	RDEF Switcher.rdef
)
UsePrivateHeaders(Switcher shared interface app)
