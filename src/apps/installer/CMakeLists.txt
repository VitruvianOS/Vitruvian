Application(Installer
	SOURCES
	EULAWindow.cpp
	InstallerApp.cpp
	InstallerDefs.cpp
	InstallerWindow.cpp
	PartitionMenuItem.cpp
	ProgressReporter.cpp
	WorkerThread.cpp
	LIBS tracker shared translation localestub
	RDEF Installer.rdef
)
UsePrivateHeaders(Installer app interface shared storage tracker system)
DoCatalogs("x-vnd.Haiku-Installer" apps/installer)
