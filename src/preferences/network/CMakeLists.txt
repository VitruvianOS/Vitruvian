Application(Network
	SOURCES
	Network.cpp
	NetworkWindow.cpp
	InterfaceDetailView.cpp
	MobileBroadbandView.cpp
	StaticIPView.cpp
	IPAddressControl.cpp
	NetworkHotspotSettings.cpp
	ProxyView.cpp
	ProxySettings.cpp
	LIBS be shared localestub tracker
	RDEF Network.rdef
)

target_include_directories(Network PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/src/kits/network/libnetapi
)
DoCatalogs("x-vnd.Haiku-Network" preferences/network)