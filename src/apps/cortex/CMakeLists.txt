Application(
	Cortex

	SOURCES
	CortexApp.cpp
	CortexDump.cpp
	CortexGraphView.cpp
	CortexNodes.cpp
	CortexWindow.cpp
	LIBS be shared localestub media2
	RDEF Cortex.rdef
)

target_include_directories(Cortex PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
