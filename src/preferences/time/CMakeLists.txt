Application(Time
	SOURCES
	AnalogClock.cpp
	BaseView.cpp
	ClockView.cpp
	DateTimeView.cpp
	NetworkTimeView.cpp
	Time.cpp
	TimeSettings.cpp
	TimeWindow.cpp
	TimeZoneListItem.cpp
	TimeZoneListView.cpp
	TZDisplay.cpp
	TimedatedAsync.cpp
	ZoneView.cpp
	LIBS shared localestub
	RDEF Time.rdef
)
UsePrivateHeaders(Time locale)
DoCatalogs("x-vnd.Haiku-Time" preferences/time)
