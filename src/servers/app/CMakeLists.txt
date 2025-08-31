find_package(Freetype REQUIRED)

find_library(DRM_LIBRARY NAMES drm)
find_path(DRM_INCLUDE_DIR NAMES libdrm/drm.h)

Server(
	app_server

	SOURCES
	Angle.cpp
	AppServer.cpp
	#BitfieldRegion.cpp
	BitmapManager.cpp
	Canvas.cpp
	ClientMemoryAllocator.cpp
	CursorData.cpp
	CursorManager.cpp
	CursorSet.cpp
	DelayedMessage.cpp
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
	Layer.cpp
	MessageLooper.cpp
	OffscreenServerWindow.cpp
	OffscreenWindow.cpp
	PictureBoundingBoxPlayer.cpp
	ProfileMessageSupport.cpp
	RGBColor.cpp
	RegionPool.cpp
	Screen.cpp
	ScreenConfigurations.cpp
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

	decorator/Decorator.cpp
	decorator/DecorManager.cpp
	decorator/DefaultDecorator.cpp
	decorator/DefaultWindowBehaviour.cpp
	decorator/MagneticBorder.cpp
	decorator/TabDecorator.cpp
	decorator/WindowBehaviour.cpp

	font/FontCache.cpp
	font/FontCacheEntry.cpp
	font/FontEngine.cpp
	font/FontFamily.cpp
	font/FontManager.cpp
	font/FontStyle.cpp

	# TODO this should be
	#libasdrawing.a
	drawing/AlphaMask.cpp
	drawing/AlphaMaskCache.cpp
	drawing/BitmapBuffer.cpp
	drawing/BitmapDrawingEngine.cpp
	drawing/drawing_support.cpp
	drawing/DrawingEngine.cpp
	drawing/MallocBuffer.cpp
	drawing/UpdateQueue.cpp
	drawing/PatternHandler.cpp
	drawing/Overlay.cpp
	drawing/BitmapHWInterface.cpp
	drawing/BBitmapBuffer.cpp
	drawing/HWInterface.cpp

	drawing/Painter/GlobalSubpixelSettings.cpp
	drawing/Painter/Painter.cpp
	drawing/Painter/Transformable.cpp
	# drawing_modes
	drawing/Painter/drawing_modes/PixelFormat.cpp
	# bitmap_painter
	drawing/Painter/bitmap_painter/BitmapPainter.cpp
	drawing/Painter/AGGTextRenderer.cpp

	#drawing/interface/remote/NetReceiver.cpp
	#drawing/interface/remote/NetSender.cpp
	#drawing/interface/remote/RemoteDrawingEngine.cpp
	#drawing/interface/remote/RemoteEventStream.cpp
	#drawing/interface/remote/RemoteHWInterface.cpp
	#drawing/interface/remote/RemoteMessage.cpp
	#drawing/interface/remote/StreamingRingBuffer.cpp

	#drawing/interface/local/AccelerantBuffer.cpp
	#drawing/interface/local/AccelerantHWInterface.cpp

	drawing/interface/linux/drm/DrmHWInterface.cpp
	drawing/interface/linux/drm/DrmBuffer.cpp
	drawing/interface/linux/drm/modeset.c

	drawing/interface/linux/fbdev/FBDevHWInterface.cpp
	drawing/interface/linux/fbdev/FBDevBuffer.cpp

	drawing/interface/linux/LibInputEventStream.cpp

	stackandtile/SATDecorator.cpp
	stackandtile/SATGroup.cpp
	stackandtile/SATWindow.cpp
	stackandtile/StackAndTile.cpp
	stackandtile/Stacking.cpp
	stackandtile/Tiling.cpp

	LIBS be root localestub shared agg icon linprog ${FREETYPE_LIBRARY} ${INPUT_LIBRARIES} ${UDEV_LIBRARIES} ${DRM_LIBRARIES}

	INCLUDES
	${DRM_INCLUDE_DIR}/libdrm/
	${FREETYPE_INCLUDE_DIRS}

	"./drawing"
	"./drawing/interface/linux"
	"./drawing/interface/linux/drm"
	"./drawing/interface/linux/fbdev"
	"./drawing/interface/local"
	"./drawing/interface/remote"
	"./font"
	"./stackandtile"
	"./decorator"
	"./drawing/Painter"
	"./drawing/Painter/drawing_modes"
)

UsePrivateHeaders(app_server app interface graphics graphics/common kernel support)

#TODO: we want a custom target_include_directories that does that:
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -iquote ${PROJECT_SOURCE_DIR}/src/servers/app/")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -iquote ${PROJECT_SOURCE_DIR}/src/servers/app/ \
	-iquote ${PROJECT_SOURCE_DIR}/src/servers/app/drawing/Painter/bitmap_painter/")
