add_definitions(-DDB_ADDONS -D_INCLUDES_CLASS_DEVICE_MAP=1
	-D_SHOW_CALENDAR_MENU_WINDOW=1 -DMOUNT_MENU_IN_DESKBAR=1
	-DDB_ADDONS -D_INCLUDES_CLASS_DEVICE_MAP=1
	-D_SUPPORTS_RESOURCES=1
	-D_SUPPORTS_FEATURE_SCRIPTING=1
)

Application(
	Deskbar

	SOURCES
	BarApp.cpp
	BarMenuBar.cpp
	BarMenuTitle.cpp
	BarView.cpp
	BarWindow.cpp
	CalendarMenuWindow.cpp
	DeskbarMenu.cpp
	DeskbarUtils.cpp
	ExpandoMenuBar.cpp
	InlineScrollView.cpp
	PreferencesWindow.cpp
	ShowHideMenuItem.cpp
	StatusView.cpp
	StatusViewShelf.cpp
	TeamMenu.cpp
	TeamMenuItem.cpp
	TimeView.cpp
	TruncatableMenuItem.cpp
	WindowMenu.cpp
	WindowMenuItem.cpp
	ResourceSet.cpp
	Switcher.cpp

	LIBS tracker libshared
	RDEF Deskbar.rdef
)

UsePrivateHeaders(Deskbar app interface)
