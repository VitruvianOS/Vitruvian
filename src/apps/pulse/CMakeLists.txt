Application(Pulse
	SOURCES
	ConfigView.cpp
	CPUButton.cpp
	DeskbarPulseView.cpp
	MiniPulseView.cpp
	NormalPulseView.cpp
	Prefs.cpp
	PrefsWindow.cpp
	ProgressBar.cpp
	PulseApp.cpp
	PulseView.cpp
	PulseWindow.cpp
	LIBS localestub
	RDEF Pulse.rdef
)
UsePrivateHeaders(Pulse shared interface app system)
