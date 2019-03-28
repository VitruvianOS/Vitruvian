find_package(Freetype REQUIRED)
include_directories(${FREETYPE_INCLUDE_DIRS})

add_executable(app_server)

target_include_directories(app_server PRIVATE
	"./"
	"./drawing"
	"./font"
	"./stackandtile"
	"./decorator"
	"./drawing/Painter"
	"./drawing/Painter/drawing_modes"
)

target_sources(
	app_server
	PRIVATE

	Angle.cpp
	AppServer.cpp
	BitmapManager.cpp
	ClientMemoryAllocator.cpp
	CursorData.cpp
	CursorManager.cpp
	CursorSet.cpp
	Desktop.cpp
	DesktopListener.cpp
	DesktopSettings.cpp
	DirectWindowInfo.cpp
	DrawState.cpp
	EventDispatcher.cpp
	EventStream.cpp
	HashTable.cpp
	InputManager.cpp
	IntPoint.cpp
	IntRect.cpp
	MessageLooper.cpp
	MultiLocker.cpp
	OffscreenServerWindow.cpp
	OffscreenWindow.cpp
	ProfileMessageSupport.cpp
	RegionPool.cpp
	RGBColor.cpp
	ScreenConfigurations.cpp
	Screen.cpp
	ScreenManager.cpp
	ServerApp.cpp
	ServerBitmap.cpp
	ServerCursor.cpp
	ServerFont.cpp
	ServerPicture.cpp
	ServerWindow.cpp
	SystemPalette.cpp
	View.cpp
	VirtualScreen.cpp
	Window.cpp
	WindowList.cpp
	Workspace.cpp
	WorkspacesView.cpp
)

target_link_libraries(app_server PRIVATE be root freetype agg icon linprog)
