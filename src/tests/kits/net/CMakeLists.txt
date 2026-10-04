UsePrivateHeaders(GLOBAL net)

add_subdirectory(service)

Test(NetAddressTest SOURCES NetAddressTest.cpp)
Test (NetEndpointTest SOURCES NetEndpointTest.cpp)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO REQUIRED gio-2.0)

# Host test for ModemManagerBackend: compiles the backend against local
# shims so it runs on a plain Linux host without the V\OS kernel.
add_executable(ModemManagerBackendHostTest
	libnetapi/ModemManagerBackendHostTest.cpp
	libnetapi/hostshims/stubs.cpp
	${CMAKE_SOURCE_DIR}/src/kits/network/libnetapi/ModemManagerBackend.cpp
)
target_include_directories(ModemManagerBackendHostTest BEFORE PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/libnetapi/hostshims
	${CMAKE_CURRENT_SOURCE_DIR}/libnetapi
	${CMAKE_SOURCE_DIR}/src/kits/network/libnetapi
)
target_include_directories(ModemManagerBackendHostTest PRIVATE ${GIO_INCLUDE_DIRS})
target_link_libraries(ModemManagerBackendHostTest PRIVATE ${GIO_LIBRARIES} pthread)
target_compile_options(ModemManagerBackendHostTest PRIVATE ${GIO_CFLAGS_OTHER})
