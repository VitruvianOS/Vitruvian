# Test binaries and test suites
set(TEST_BINARIES
	UnitTester
	compression_test
	string_utf8_tests
	PortLinkTest
	HandlerLooperMessageTest
	NodeMonitorTest
	PathMonitorTest
	PathMonitorTest2
	RegistrarTest1
)

set(TEST_VOS_BINARIES
	testlist
	testoskit
	testports
	testports2
	testsem
	testsemdeletion
	testthread
	testteam
	testfsinfo
	test_team_send_data
	set_port_owner
	test_load_image
	test_load_image2
	test_area
	testvref
	teststopwatch
	testinput
	testinject
	testwatch
	testxkbfuzz
	#test_node_monitor
	#test_node_monitor2
	writer
	reader
	order
)

set(TEST_KERNEL_BINARIES
	fibo_load_image
	path_resolution_test
	port_close_test_1
	port_close_test_2
	port_delete_test
	port_multi_read_test
	port_wakeup_test_1
	port_wakeup_test_2
	port_wakeup_test_3
	port_wakeup_test_4
	port_wakeup_test_5
	port_wakeup_test_6
	port_wakeup_test_7
	port_wakeup_test_8
	port_wakeup_test_9
	sem_acquire_test1
	wait_for_objects_test
)

set(TEST_LIBROOT_BINARIES
	system_watching_test
)

# Interface kit manual tests. LookTest and ControlLookTest are widget
# galleries, the rest are drawing, input and timer checks that happen to
# live alongside them.
set(TEST_GUI_BINARIES
	LookTest
	ControlLookTest
	ChannelSliderTest
	ListViewTest
	MenuBeginningTest
	ScrollViewTest
	SimpleAlertTest
	SliderTest
	StatusBarTest
	TextViewTestManual
	ToolTipTest
	WindowStackTest
	ClippingPlusRedraw
	DrawBitmapTest
	GetMouseTest
	PulseTest
	ScreenTest
	SetDiskModeTest
	TruncateString
)

set(TEST_ADDONS
	supporttest
	storagetest
	apptest
	interfacetest
	sharedtest
	libexampletest
)

ImageInclude("/system/tests" ${TEST_BINARIES} ${TEST_VOS_BINARIES} ${TEST_KERNEL_BINARIES} ${TEST_LIBROOT_BINARIES} cppunit)
ImageInclude("/system/tests/interface" ${TEST_GUI_BINARIES})
ImageInclude("/system/tests/lib" ${TEST_ADDONS})
ImageIncludeFile("src/tests/runsuite.sh" "/system/tests")
ImageIncludeDir("src/tests/kits/storage/resources" "/system/tests")
