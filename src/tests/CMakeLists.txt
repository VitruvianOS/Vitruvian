add_subdirectory(apps)
add_subdirectory(kits)
add_subdirectory(system)
add_subdirectory(vos)

AddOn(libexampletest SHARED SOURCES ExampleTestAddon.cpp ExampleTest.cpp)
#UnitTester(UnitTester SOURCES UnitTester.cpp RDEF UnitTester.rdef)
Application(UnitTester SOURCES UnitTester.cpp LIBS libcppunit)
