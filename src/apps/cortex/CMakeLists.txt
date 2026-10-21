Application(
	Cortex

	SOURCES
	CortexApp.cpp
	CortexDeviceControls.cpp
	CortexDump.cpp
	CortexGraphView.cpp
	CortexMonitor.cpp
	CortexNodes.cpp
	CortexWindow.cpp
	LIBS be shared localestub media2 tracker
	RDEF Cortex.rdef
)

UsePrivateHeaders(Cortex media2)

target_include_directories(Cortex PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
