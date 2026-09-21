Application(DriveSetup
	SOURCES
	AbstractParametersPanel.cpp
	ChangeParametersPanel.cpp
	CreateParametersPanel.cpp
	DiskView.cpp
	DriveSetup.cpp
	EncryptionUtils.cpp
	FeaturesWindow.cpp
	FlagsPanel.cpp
	InitParametersPanel.cpp
	MainWindow.cpp
	PartitionList.cpp
	ProgressWindow.cpp
	RenamePanel.cpp
	ResizeMoveWindow.cpp
	Support.cpp
	LIBS shared tracker localestub
	RDEF DriveSetup.rdef
)
UsePrivateHeaders(DriveSetup interface shared storage tracker mount)
DoCatalogs("x-vnd.Haiku-DriveSetup" apps/drivesetup)

# SetFlagsJob/PartitionPlanBuilder/DiskDeviceJobQueue live under the storage
# kit's disk_device/ tree, not headers/private/storage; the kit target adds
# them via its own target_include_directories(be ...).
target_include_directories(DriveSetup PRIVATE
	${PROJECT_SOURCE_DIR}/src/kits/storage/disk_device
	${PROJECT_SOURCE_DIR}/src/kits/storage/disk_device/jobs
)
