Application(ProcessController
	SOURCES
	AutoIcon.cpp
	IconMenuItem.cpp
	KernelMemoryBarMenuItem.cpp
	MemoryBarMenu.cpp
	MemoryBarMenuItem.cpp
	NoiseBarMenuItem.cpp
	PCWindow.cpp
	PCWorld.cpp
	Preferences.cpp
	PriorityMenu.cpp
	ProcessController.cpp
	QuitMenu.cpp
	TeamBarMenu.cpp
	TeamBarMenuItem.cpp
	ThreadBarMenu.cpp
	ThreadBarMenuItem.cpp
	Utilities.cpp
	LIBS shared localestub
	RDEF ProcessController.rdef
)
UsePrivateHeaders(ProcessController app interface shared system)
