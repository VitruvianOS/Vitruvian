Application(Network
	SOURCES
	Network.cpp
	NetworkWindowNM.cpp
	InterfaceDetailView.cpp
	StaticIPView.cpp
	IPAddressControl.cpp
	LIBS be shared localestub
	RDEF Network.rdef
)

target_include_directories(Network PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/src/kits/network/libnetapi
)
DoCatalogs("x-vnd.Haiku-Network" preferences/network)