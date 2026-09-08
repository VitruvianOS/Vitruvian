Application(PackageManager
	SOURCES
	AptBackend.cpp
	AptLogView.cpp
	FilterView.cpp
	MainWindow.cpp
	PackageInfo.cpp
	PackageInfoView.cpp
	PackageListView.cpp
	PackageManagerApp.cpp
	PackageRow.cpp
	PackageWorker.cpp
	WorkStatusView.cpp
	LIBS shared tracker localestub vpackage apt-pkg
	RDEF PackageManager.rdef
)
UsePrivateHeaders(PackageManager app interface shared storage tracker)
DoCatalogs("application/x-vnd.Vitruvian-PackageManager" apps/packagemanager)
