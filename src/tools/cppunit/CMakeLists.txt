#TODO: It may be interesting to see this work on Linux
add_definitions(-DNO_ELF_SYMBOL_PATCHING)

AddOn(
	libcppunit
	SHARED
	SOURCES
	BTestCase.cpp
	BTestSuite.cpp
	SemaphoreSyncObject.cpp
	TestApp.cpp
	TestListener.cpp
	TestShell.cpp
	TestUtils.cpp
	ThreadedTestCase.cpp

	cppunit/Asserter.cpp
	cppunit/CompilerOutputter.cpp
	cppunit/Exception.cpp
	cppunit/NotEqualException.cpp
	cppunit/RepeatedTest.cpp
	cppunit/SourceLine.cpp
	cppunit/SynchronizedObject.cpp
	cppunit/TestAssert.cpp
	cppunit/TestCase.cpp
	cppunit/TestFactoryRegistry.cpp
	cppunit/TestFailure.cpp
	cppunit/TestResult.cpp
	cppunit/TestResultCollector.cpp
	cppunit/TestRunner.cpp
	cppunit/TestSetUp.cpp
	cppunit/TestSucessListener.cpp
	cppunit/TestSuite.cpp
	cppunit/TextOutputter.cpp
	cppunit/TextTestProgressListener.cpp
	cppunit/TextTestResult.cpp
	cppunit/TypeInfoHelper.cpp
	cppunit/XmlOutputter.cpp
)

target_include_directories(libcppunit PRIVATE "${PROJECT_SOURCE_DIR}/headers/tools/cppunit")
