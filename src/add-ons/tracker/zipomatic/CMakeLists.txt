TrackerAddOn(
	ZipOMatic SHARED

	SOURCES
	GenericThread.cpp
	ZipOMatic.cpp
	ZipOMaticActivity.cpp
	ZipOMaticWindow.cpp
	ZipperThread.cpp

	LIBS be tracker localestub

	RDEF ZipOMatic.rdef
)

UsePrivateHeaders(ZipOMatic tracker shared interface storage support)
DoCatalogs("x-vnd.vos.zip-o-matic" add-ons/tracker/zipomatic)
