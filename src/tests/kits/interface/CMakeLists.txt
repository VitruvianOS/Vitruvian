include_directories("../../../../headers/private/interface")

Test (SimpleAlertTest SOURCES SimpleAlertTest.cpp)

Test (ChannelSliderTest SOURCES ChannelSliderTest.cpp)

#Test (CheckBoxTest SOURCES CheckBoxTest.cpp)

Test (ControlLookTest SOURCES ControlLookTest.cpp)

Test (DrawBitmapTest SOURCES bbitmap/DrawBitmapTest.cpp)

Test (ListViewTest SOURCES ListViewTest.cpp)

Test (ScreenTest SOURCES ScreenTest.cpp)

Test (ScrollViewTest SOURCES ScrollViewTest.cpp)

Test (StatusBarTest SOURCES StatusBarTest.cpp)

Test (ToolTipTest SOURCES ToolTipTest.cpp)

#Test (GetMouseTest SOURCES GetMouseTest.cpp)

Test (PulseTest SOURCES PulseTest.cpp)

Test (SliderTest SOURCES SliderTest.cpp)

Test (TruncateString SOURCES TruncateString.cpp)

Test (MenuBeginningTest SOURCES MenuBeginningTest.cpp)

#Test (WidthBufferTest SOURCES WidthBufferTest.cpp)

Test (ClippingPlusRedraw SOURCES ClippingPlusRedraw.cpp)

Test (SetDiskModeTest SOURCES SetDiskModeTest.cpp)

#Test (ScrollViewSetBorderTest SOURCES ScrollViewSetBorderTest.cpp)

Test (TextViewTestManual SOURCES TextViewTestManual.cpp)

Test (WindowStackTest SOURCES bwindowstack/WindowStackTest.cpp)

Application(
	LookTest

	SOURCES
	look/Look.cpp
	INCLUDES
	"../../../../headers/private/interface"
)

target_link_libraries(LookTest PRIVATE tracker)
