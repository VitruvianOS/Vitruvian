include_directories("../../../../headers/private/net")

add_subdirectory(service)

Test (NetAddressTest SOURCES NetAddressTest.cpp)
Test (NetEndpointTest SOURCES NetEndpointTest.cpp)
