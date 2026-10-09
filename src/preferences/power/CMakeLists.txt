Application(Power
	SOURCES
	Power.cpp
	PowerView.cpp
	PowerWindow.cpp
	${CMAKE_SOURCE_DIR}/src/apps/powerstatus/ACPIDriverInterface.cpp
	${CMAKE_SOURCE_DIR}/src/apps/powerstatus/APMDriverInterface.cpp
	${CMAKE_SOURCE_DIR}/src/apps/powerstatus/DriverInterface.cpp
	${CMAKE_SOURCE_DIR}/src/apps/powerstatus/SysFSDriverInterface.cpp
	LIBS be shared localestub systemd
	RDEF Power.rdef
)

target_include_directories(Power PRIVATE
	${CMAKE_SOURCE_DIR}/src/apps/powerstatus
	${CMAKE_SOURCE_DIR}/headers/private/device
)
DoCatalogs("x-vnd.Haiku-Power" preferences/power)
