add_subdirectory(testharness)

Application(testinput SOURCES testinput.cpp)

Application(testxkbfuzz SOURCES testxkbfuzz.cpp LIBS shared)
UsePrivateHeaders(testxkbfuzz shared)

Application(testwatch SOURCES testwatch.cpp)

Application(testsuspend SOURCES testsuspend.cpp)

add_executable(testinject testinject.cpp)

# Unconfirmed display change revert state machine (pure helpers, the
# same sources app_server links; also runnable on a plain host).
add_executable(test_display_change_tracker
	test_display_change_tracker.cpp
	${CMAKE_SOURCE_DIR}/src/servers/app/DisplayChangeTracker.cpp
)
target_include_directories(test_display_change_tracker PRIVATE
	${CMAKE_SOURCE_DIR}/src/servers/app)

# Output add-on transport core (pure helpers, no window server).
add_executable(test_output_addon_core
	output_addon/output_addon_host_test.cpp
	${CMAKE_SOURCE_DIR}/src/kits/interface/OutputAddonCore.cpp
)
target_include_directories(test_output_addon_core PRIVATE
	${CMAKE_SOURCE_DIR}/headers/private/interface)
set_target_properties(test_output_addon_core PROPERTIES COMPILE_FLAGS
	"-include LinuxBuildCompatibility.h")

# Pointer-constraint math, grab arbitration and the raw-event valuator
# helpers (pure, the same sources input_server and app_server link).
add_executable(test_input_control
	input_control/input_control_host_test.cpp
	${CMAKE_SOURCE_DIR}/src/kits/shared/PointerControlCore.cpp
)
target_include_directories(test_input_control PRIVATE
	${CMAKE_SOURCE_DIR}/headers/private/input
	${CMAKE_SOURCE_DIR}/headers/os/support)
set_target_properties(test_input_control PROPERTIES COMPILE_FLAGS
	"-include LinuxBuildCompatibility.h")


# Panel orientation rules (header only, no window server).
Application(test_panel_orientation
	SOURCES test_panel_orientation.cpp)
UsePrivateHeaders(test_panel_orientation interface)

# CUPS end-to-end test (V\OS printing, no print server).
Application(test_printcups
	SOURCES printcups/test_printcups.cpp
	LIBS be printcups ${CUPS_LIBRARIES})
UsePrivateHeaders(test_printcups print interface support)

#Application(test_node_monitor SOURCES test_node_monitor.cpp)
#Application(test_node_monitor2 SOURCES test_node_monitor2.cpp)

#UsePrivateHeaders(test_node_monitor kernel)
#UsePrivateHeaders(test_node_monitor2 kernel)
