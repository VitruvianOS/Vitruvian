add_subdirectory(apps)
add_subdirectory(kits)
add_subdirectory(system)
add_subdirectory(vos)

add_definitions(-DNO_ELF_SYMBOL_PATCHING)

include_directories("${PROJECT_SOURCE_DIR}/headers/tools/cppunit")


AddOn(libexampletest SHARED SOURCES ExampleTestAddon.cpp ExampleTest.cpp)
#UnitTester(UnitTester SOURCES UnitTester.cpp RDEF UnitTester.rdef)
Application(UnitTester SOURCES UnitTester.cpp LIBS be root libcppunit)
