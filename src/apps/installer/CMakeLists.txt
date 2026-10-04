Application(Installer
	SOURCES
	EULAWindow.cpp
	InstallerApp.cpp
	InstallerDefs.cpp
	InstallerWindow.cpp
	PartitionMenuItem.cpp
	ProgressReporter.cpp
	TargetSetupWindow.cpp
	WorkerThread.cpp
	LIBS tracker shared translation localestub
	RDEF Installer.rdef
)
UsePrivateHeaders(Installer app interface shared storage tracker system)
DoCatalogs("x-vnd.vos-Installer" apps/installer)

# PartitionPlanBuilder lives under the storage kit's disk_device/ tree;
# the kit target adds that include dir only for itself.
target_include_directories(Installer PRIVATE
	${PROJECT_SOURCE_DIR}/src/kits/storage/disk_device
	${PROJECT_SOURCE_DIR}/src/kits/storage/disk_device/jobs
)
