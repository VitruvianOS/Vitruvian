Application(Installer
	SOURCES
	CopyEngine.cpp
	EULAWindow.cpp
	InstallerApp.cpp
	InstallerDefs.cpp
	InstallerWindow.cpp
	PackageViews.cpp
	PartitionMenuItem.cpp
	ProgressReporter.cpp
	UnzipEngine.cpp
	WorkerThread.cpp
	LIBS tracker shared translation localestub
	RDEF Installer.rdef
)
UsePrivateHeaders(Installer interface shared storage tracker system)
