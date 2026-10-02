add_subdirectory(testharness)

Application(testinput SOURCES testinput.cpp)

Application(testxkbfuzz SOURCES testxkbfuzz.cpp LIBS shared)
UsePrivateHeaders(testxkbfuzz shared)

Application(testwatch SOURCES testwatch.cpp)

Application(testsuspend SOURCES testsuspend.cpp)

add_executable(testinject testinject.cpp)

# Screen-change relayout policy (pure helpers, no window server).
Application(test_screen_relayout
	SOURCES test_screen_relayout.cpp
		${CMAKE_SOURCE_DIR}/src/servers/app/ScreenRelayout.cpp
	INCLUDES ${CMAKE_SOURCE_DIR}/src/servers/app)
UsePrivateHeaders(test_screen_relayout interface)

# CUPS end-to-end test (V\OS printing, no print server).
Application(test_printcups
	SOURCES printcups/test_printcups.cpp
	LIBS be printcups ${CUPS_LIBRARIES})
UsePrivateHeaders(test_printcups print interface support)

#Application(test_node_monitor SOURCES test_node_monitor.cpp)
#Application(test_node_monitor2 SOURCES test_node_monitor2.cpp)

#UsePrivateHeaders(test_node_monitor kernel)
#UsePrivateHeaders(test_node_monitor2 kernel)
