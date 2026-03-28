Application(PowerStatus
	SOURCES
	ACPIDriverInterface.cpp
	APMDriverInterface.cpp
	DriverInterface.cpp
	ExtendedInfoWindow.cpp
	PowerStatus.cpp
	PowerStatusView.cpp
	PowerStatusWindow.cpp
	LIBS shared translation localestub
	RDEF PowerStatus.rdef
)
UsePrivateHeaders(PowerStatus interface shared)
target_include_directories(PowerStatus PRIVATE
	${PROJECT_SOURCE_DIR}/headers/private/device)
