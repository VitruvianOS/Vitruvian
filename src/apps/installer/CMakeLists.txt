Application(Installer
	SOURCES
	CopyEngine.cpp
	InstallerApp.cpp
	InstallerDefs.cpp
	InstallerWindow.cpp
	PartitionMenuItem.cpp
	ProgressReporter.cpp
	WorkerThread.cpp
	LIBS tracker shared translation localestub
	RDEF Installer.rdef
)
UsePrivateHeaders(Installer interface shared storage tracker system)
