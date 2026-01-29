add_subdirectory(apps)
add_subdirectory(kits)
add_subdirectory(servers)
add_subdirectory(system)
add_subdirectory(vos)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
add_definitions(-DNO_ELF_SYMBOL_PATCHING)
endif()

include_directories("${PROJECT_SOURCE_DIR}/headers/tools/cppunit")

AddOn(libexampletest SHARED SOURCES ExampleTestAddon.cpp ExampleTest.cpp)
Application(UnitTester SOURCES UnitTester.cpp LIBS be root cppunit)
