add_subdirectory(testharness)

Application(test_node_monitor SOURCES test_node_monitor.cpp)
Application(test_node_monitor2 SOURCES test_node_monitor2.cpp)

UsePrivateHeaders(test_node_monitor kernel)
UsePrivateHeaders(test_node_monitor2 kernel)
