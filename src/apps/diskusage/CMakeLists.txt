Application(DiskUsage
	SOURCES
	App.cpp
	ControlsView.cpp
	DiskUsage.cpp
	InfoWindow.cpp
	MainWindow.cpp
	PieView.cpp
	Scanner.cpp
	Snapshot.cpp
	StatusView.cpp
	VolumeView.cpp
	LIBS shared localestub
	RDEF DiskUsage.rdef
)
UsePrivateHeaders(DiskUsage mount shared tracker storage)
