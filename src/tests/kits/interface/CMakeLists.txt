UsePrivateHeaders(GLOBAL interface)

add_definitions(-DNO_ELF_SYMBOL_PATCHING -DTEST_OBOS)

AddOn(
	interfacetest
	SHARED

	SOURCES
	InterfaceKitTestAddon.cpp
	balert/AlertTest.cpp
	bbitmap/BitmapTest.cpp
	bbitmap/BBitmapTester.cpp
	bbitmap/SetBitsTester.cpp
	bdeskbar/DeskbarTest.cpp
	bdeskbar/DeskbarAddItemTest.cpp
	bdeskbar/DeskbarGetItemTest.cpp
	bdeskbar/DeskbarLocationTest.cpp
	bpolygon/PolygonTest.cpp
	bpolygon/CreatePolygonTest.cpp
	bpolygon/DummyPolygon.cpp
	bpolygon/MapPolygonTest.cpp
	bmenu/MenuTest.cpp
	bregion/RegionTest.cpp
	bregion/RegionTestcase.cpp
	bregion/RegionConstruction.cpp
	bregion/RegionExclude.cpp
	bregion/RegionInclude.cpp
	bregion/RegionIntersect.cpp
	bregion/RegionOffsetBy.cpp
	btextcontrol/TextControlTest.cpp
	btextview/TextViewTest.cpp
	GraphicsDefsTest.cpp
	OutlineListViewTest.cpp

	INCLUDES
	"${PROJECT_SOURCE_DIR}/headers/tools/cppunit/"
)

Test(ChannelSliderTest SOURCES ChannelSliderTest.cpp)

#Test(CheckBoxTest SOURCES CheckBoxTest.cpp)

Test(ControlLookTest SOURCES ControlLookTest.cpp)

Test(ClippingPlusRedraw SOURCES ClippingPlusRedraw.cpp)

Test(DrawBitmapTest SOURCES bbitmap/DrawBitmapTest.cpp)

Test(GetMouseTest SOURCES GetMouseTest.cpp)

Test(LookTest SOURCES look/Look.cpp LIBS tracker)

Test(ListViewTest SOURCES ListViewTest.cpp)

Test(MenuBeginningTest SOURCES MenuBeginningTest.cpp)

Test(PulseTest SOURCES PulseTest.cpp)

Test(ScreenTest SOURCES ScreenTest.cpp)

#Test(ScrollViewSetBorderTest SOURCES ScrollViewSetBorderTest.cpp)

Test(ScrollViewTest SOURCES ScrollViewTest.cpp)

Test(SetDiskModeTest SOURCES SetDiskModeTest.cpp)

Test(SimpleAlertTest SOURCES SimpleAlertTest.cpp)

Test(SliderTest SOURCES SliderTest.cpp)

Test(StatusBarTest SOURCES StatusBarTest.cpp)

Test(TextViewTestManual SOURCES TextViewTestManual.cpp)

Test(ToolTipTest SOURCES ToolTipTest.cpp)

Test(TruncateString SOURCES TruncateString.cpp)

#Test(WidthBufferTest SOURCES WidthBufferTest.cpp)

Test(WindowStackTest SOURCES bwindowstack/WindowStackTest.cpp)
