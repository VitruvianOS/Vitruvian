find_package(Freetype REQUIRED)
include_directories(${FREETYPE_INCLUDE_DIRS})

add_executable(app_server
	Angle.cpp
	AppServer.cpp
	BitmapDrawingEngine.cpp
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

	font/FontCache.cpp
	font/FontCacheEntry.cpp
	font/FontEngine.cpp
	font/FontFamily.cpp
	font/FontManager.cpp
	font/FontStyle.cpp
	
	decorator/Decorator.cpp
	decorator/DecorManager.cpp
	decorator/DefaultDecorator.cpp
	decorator/DefaultWindowBehaviour.cpp
	decorator/MagneticBorder.cpp
	decorator/WindowBehaviour.cpp
	
	drawing/AccelerantHWInterface.cpp
	drawing/BBitmapBuffer.cpp
	drawing/BitmapBuffer.cpp
	drawing/BitmapHWInterface.cpp
	drawing/DrawingEngine.cpp
	drawing/drawing_support.cpp
	drawing/DWindowBuffer.cpp
	drawing/DWindowHWInterface.cpp
	drawing/HWInterface.cpp
	drawing/MallocBuffer.cpp
	drawing/Overlay.cpp
	drawing/PatternHandler.cpp
	drawing/UpdateQueue.cpp
	drawing/ViewHWInterface.cpp

	drawing/Painter/AGGTextRenderer.cpp
	drawing/Painter/GlobalSubpixelSettings.cpp
	drawing/Painter/Painter.cpp
	drawing/Painter/Transformable.cpp

	drawing/AccelerantBuffer.cpp
	drawing/AccelerantHWInterface.cpp
	drawing/BBitmapBuffer.cpp
	drawing/BitmapBuffer.cpp
	drawing/BitmapHWInterface.cpp
	drawing/DrawingEngine.cpp
	drawing/drawing_support.cpp
	drawing/DWindowBuffer.cpp
	drawing/DWindowHWInterface.cpp
	drawing/HWInterface.cpp
	drawing/MallocBuffer.cpp
	drawing/Overlay.cpp
	drawing/PatternHandler.cpp
	drawing/UpdateQueue.cpp
	drawing/ViewHWInterface.cpp

	stackandtile/SATDecorator.cpp
	stackandtile/SATGroup.cpp
	stackandtile/SATWindow.cpp
	stackandtile/StackAndTile.cpp
	stackandtile/Stacking.cpp
	stackandtile/Tiling.cpp

	drawing/Painter/drawing_modes/PixelFormat.cpp
)


target_link_libraries(app_server PUBLIC root)
target_link_libraries(app_server PUBLIC be)
target_link_libraries(app_server PUBLIC freetype)
target_link_libraries(app_server PUBLIC agg)
target_link_libraries(app_server PUBLIC icon)
target_link_libraries(app_server PUBLIC linprog)
