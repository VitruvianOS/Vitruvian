find_package(Catch2 3 REQUIRED)
include(Catch)

set(UNIT_TEST_UTILS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/utils")

# UnitTest(name SOURCES <files...> [LIBS <libs...>])
#
# Builds a Catch2 unit test executable against the Vitruvian API and registers
# each of its test cases with CTest. libbe and libroot are always linked, and
# the headers in utils/ are on the include path.
function(UnitTest name)
	cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})

	add_executable(${name} ${ARG_SOURCES})
	target_include_directories(${name} PRIVATE "${UNIT_TEST_UTILS_DIR}")
	target_compile_options(${name} PRIVATE -include LinuxBuildCompatibility.h)
	target_link_libraries(${name} PRIVATE be root ${ARG_LIBS} Catch2::Catch2WithMain)
	catch_discover_tests(${name} DISCOVERY_MODE PRE_TEST)
endfunction()

# These tests can use the Catch2-provided main
add_executable(example-test example-test.cpp)
target_link_libraries(example-test PRIVATE Catch2::Catch2WithMain)
catch_discover_tests(example-test)

add_subdirectory(kits)
add_subdirectory(system)
add_subdirectory(utils)
