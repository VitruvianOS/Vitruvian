UsePrivateHeaders(GLOBAL net)

# Legacy service tests need a test framework the tree does not have.
#add_subdirectory(service)

Test(NetAddressTest SOURCES NetAddressTest.cpp)
Test (NetEndpointTest SOURCES NetEndpointTest.cpp)
