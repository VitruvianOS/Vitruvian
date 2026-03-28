Application(DriveSetup
	SOURCES
	AbstractParametersPanel.cpp
	ChangeParametersPanel.cpp
	CreateParametersPanel.cpp
	DiskView.cpp
	DriveSetup.cpp
	EncryptionUtils.cpp
	InitParametersPanel.cpp
	MainWindow.cpp
	PartitionList.cpp
	Support.cpp
	LIBS shared tracker localestub
	RDEF DriveSetup.rdef
)
UsePrivateHeaders(DriveSetup interface shared storage tracker)
