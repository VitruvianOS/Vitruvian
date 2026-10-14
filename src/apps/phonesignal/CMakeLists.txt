RunnableAddOn(PhoneSignal
	SOURCES
	PhoneSignal.cpp
	PhoneSignalView.cpp
	LIBS be localestub
	RDEF PhoneSignal.rdef
)

target_include_directories(PhoneSignal PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/src/kits/network/libnetapi
)
UsePrivateHeaders(PhoneSignal interface net)
