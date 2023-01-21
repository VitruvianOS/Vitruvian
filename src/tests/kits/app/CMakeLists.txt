add_subdirectory(messaging)

UsePrivateHeaders(GLOBAL app)

add_definitions(-DNO_ELF_SYMBOL_PATCHING -DTEST_OBOS)

AddOn(
	apptest
	SHARED

	SOURCES
	AppKitTestAddon.cpp

	bapplication/ApplicationTest.cpp
	bapplication/AppQuitRequestedTester.cpp
	bapplication/AppQuitTester.cpp
	bapplication/AppRunTester.cpp
	bapplication/BApplicationTester.cpp

	bclipboard/BClipboardTester.cpp
	bclipboard/ClipboardTest.cpp
	bclipboard/CountTester.cpp
	bclipboard/LockTester.cpp
	bclipboard/ReadWriteTester.cpp

	bcursor/BCursorTester.cpp
	bcursor/CursorTest.cpp

	bhandler/HandlerTest.cpp
	bhandler/AddFilterTest.cpp
	bhandler/BHandlerTester.cpp
	bhandler/IsWatchedTest.cpp
	bhandler/LockLooperTest.cpp
	bhandler/LockLooperTestCommon.cpp
	bhandler/LockLooperWithTimeoutTest.cpp
	bhandler/HandlerLooperTest.cpp
	bhandler/NextHandlerTest.cpp
	bhandler/RemoveFilterTest.cpp
	bhandler/SetFilterListTest.cpp
	bhandler/SetNextHandlerTest.cpp
	bhandler/UnlockLooperTest.cpp

	blooper/LooperTest.cpp
	blooper/AddHandlerTest.cpp
	blooper/CountHandlersTest.cpp
	blooper/HandlerAtTest.cpp
	blooper/IndexOfTest.cpp
	blooper/IsMessageWaitingTest.cpp
	blooper/RemoveHandlerTest.cpp
	blooper/PerformTest.cpp
	blooper/RunTest.cpp
	blooper/LooperForThreadTest.cpp
	blooper/AddCommonFilterTest.cpp
	blooper/RemoveCommonFilterTest.cpp
	blooper/LooperSizeTest.cpp
	blooper/SetCommonFilterListTest.cpp
	blooper/QuitTest.cpp

	bmessagequeue/MessageQueueTest.cpp
	bmessagequeue/AddMessageTest1.cpp
	bmessagequeue/AddMessageTest2.cpp
	bmessagequeue/ConcurrencyTest1.cpp
	bmessagequeue/ConcurrencyTest2.cpp
	bmessagequeue/FindMessageTest1.cpp
	bmessagequeue/MessageQueueTestCase.cpp

	bmessagerunner/MessageRunnerTest.cpp
	bmessagerunner/BMessageRunnerTester.cpp
	broster/BroadcastTester.cpp
	bmessagerunner/GetInfoTester.cpp
	bmessagerunner/MessageRunnerTestHelpers.cpp
	bmessagerunner/SetCountTester.cpp
	bmessagerunner/SetIntervalTester.cpp

	bmessenger/MessengerTest.cpp
	bmessenger/BMessengerTester.cpp
	bmessenger/LockTargetTester.cpp
	bmessenger/LockTargetWithTimeoutTester.cpp
	bmessenger/MessengerAssignmentTester.cpp
	bmessenger/MessengerComparissonTester.cpp
	bmessenger/SendMessageTester.cpp
	bmessenger/SMInvoker.cpp
	bmessenger/SMLooper.cpp
	bmessenger/SMReplyTarget.cpp
	bmessenger/SMTarget.cpp
	bmessenger/TargetTester.cpp

	bpropertyinfo/PropertyTestcase.cpp
	bpropertyinfo/PropertyConstructionTest.cpp
	bpropertyinfo/PropertyFindMatchTest.cpp
	bpropertyinfo/PropertyFlattenTest.cpp
	bpropertyinfo/PropertyInfoTest.cpp

	broster/RosterTest.cpp
	broster/FindAppTester.cpp
	broster/GetAppInfoTester.cpp
	broster/GetAppListTester.cpp
	broster/GetRecentTester.cpp
	broster/IsRunningTester.cpp
	broster/LaunchTester.cpp
	broster/LaunchTesterHelper.cpp
	broster/testapps/RecentAppsTestApp.cpp
	broster/RosterWatchingTester.cpp
	broster/TeamForTester.cpp

	"${PROJECT_SOURCE_DIR}/src/servers/registrar/mime/RegistrarThread.cpp"
	"${PROJECT_SOURCE_DIR}/src/servers/registrar/mime/RegistrarThreadManager.cpp"
	RegistrarThreadManagerTest.cpp

	common/AppRunner.cpp
	common/PipedAppRunner.cpp
	INCLUDES
	"${PROJECT_SOURCE_DIR}/headers/tools/cppunit/"
	"${PROJECT_SOURCE_DIR}/src/servers/registrar/mime/"

	"./common"
)

AddOn(
	appteststub
	SHARED
	SOURCES
	AppKitTestStub.cpp
	INCLUDES
	"${PROJECT_SOURCE_DIR}/headers/tools/cppunit/"
)

#Test(DanoMessageTest SOURCES DanoMessageTest.cpp)
