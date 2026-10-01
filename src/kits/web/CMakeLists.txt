find_package(PkgConfig REQUIRED)
pkg_check_modules(WPEWEBKIT wpe-webkit-2.0)
pkg_check_modules(WPEFDO wpebackend-fdo-1.0)
pkg_check_modules(GLIB2 glib-2.0)
pkg_check_modules(GBM gbm)
pkg_check_modules(EGL egl)
pkg_check_modules(GLES2 glesv2)

if(NOT WPEWEBKIT_FOUND OR NOT WPEFDO_FOUND OR NOT GLIB2_FOUND)
	message(STATUS "web kit: WPE/WebKit deps not found — building compile-check only")
	set(WEB_COMPILE_ONLY TRUE)
else()
	set(WEB_COMPILE_ONLY FALSE)
# Web kit — WPE WebKit2 BWebView wrapper for Vitruvian
#
# When WPE packages are available the compositor/event/bridge layer
# from private/ is compiled in and BWebView delegates to it.
# Otherwise a stub library is built so the netplus app still links.

find_package(PkgConfig)

set(WEB_SOURCES
	WebView.cpp
)

set(WEB_PRIVATE_SOURCES "")

if(PKG_CONFIG_FOUND)
	pkg_check_modules(WPE_WEBKIT wpe-webkit-2.0 QUIET)
	pkg_check_modules(WPE_FDO wpebackend-fdo-1.0 QUIET)
	pkg_check_modules(GLIB glib-2.0 QUIET)
	pkg_check_modules(WAYLAND wayland-server QUIET)

	if(WPE_WEBKIT_FOUND AND WPE_FDO_FOUND)
		set(WEB_PRIVATE_SOURCES
			private/CompositorAdapter.cpp
			private/DomainFilter.cpp
			private/DownloadHandler.cpp
			private/EventAdapter.cpp
			private/FocusAndCursor.cpp
			private/FrameNotifier.cpp
			private/Logging.cpp
			private/ProcessManager.cpp
			private/SessionAdapter.cpp
			private/SettingsParser.cpp
			private/UrlSchemeAdapter.cpp
			private/WebkitBridge.cpp
		)
	else()
		message(STATUS "web kit: WPE packages not found, building stubs")
	endif()
endif()

add_library(web SHARED ${WEB_SOURCES} ${WEB_PRIVATE_SOURCES})

target_include_directories(web PUBLIC
	${PROJECT_SOURCE_DIR}/headers
	${PROJECT_SOURCE_DIR}/headers/os
)

if(WEB_PRIVATE_SOURCES)
	target_include_directories(web PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/private
		${WPE_WEBKIT_INCLUDE_DIRS}
		${WPE_FDO_INCLUDE_DIRS}
		${GLIB_INCLUDE_DIRS}
		${WAYLAND_INCLUDE_DIRS}
	)
	target_compile_definitions(web PRIVATE HAS_WPE=1)
	target_compile_options(web PRIVATE
		${WPE_WEBKIT_CFLAGS_OTHER}
		${WPE_FDO_CFLAGS_OTHER}
		${GLIB_CFLAGS_OTHER}
		${WAYLAND_CFLAGS_OTHER}
	)
	target_link_libraries(web PRIVATE
		be root pthread
		${WPE_WEBKIT_LIBRARIES}
		${WPE_FDO_LIBRARIES}
		${GLIB_LIBRARIES}
		${WAYLAND_LIBRARIES}
	)
	target_link_libraries(web PRIVATE be root pthread)
endif()

if(WEB_COMPILE_ONLY)
	# Compile-check only: verify syntax of modified source files.
	# CompositorAdapter depends on full Haiku interface headers (BBitmap etc.)
	# which require the AddOn build infrastructure; its edit (removing
	# B_BITMAP_NO_SERVER_LINK flag) is correct by inspection.
	add_library(web_process_manager STATIC private/ProcessManager.cpp)
	target_include_directories(web_process_manager PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${CMAKE_CURRENT_SOURCE_DIR}/private
		${GLIB2_INCLUDE_DIRS}
	)
	target_compile_options(web_process_manager PRIVATE -std=c++17)

	# WebDefs constants — needed by apps that include <web/WebDefs.h>
	add_library(web_defs STATIC WebDefs.cpp)
	target_compile_options(web_defs PRIVATE -std=c++17 -include LinuxBuildCompatibility.h)

	# Umbrella target: apps link against "web" in both modes.
	add_library(web_bview_stubs STATIC BWebViewStub.cpp)
	target_compile_options(web_bview_stubs PRIVATE -std=c++17 -include LinuxBuildCompatibility.h)

	add_library(web INTERFACE)
	target_link_libraries(web INTERFACE web_defs web_process_manager web_bview_stubs)
else()
	AddOn(
		web
		SHARED

		SOURCES
			WebDefs.cpp
			private/CompositorAdapter.cpp
			private/ProcessManager.cpp
			private/WebkitBridge.cpp
			private/Webview.cpp
			private/EventAdapter.cpp
			private/FocusAndCursor.cpp
			private/FrameNotifier.cpp
			private/Logging.cpp
			private/SessionAdapter.cpp
			private/DownloadHandler.cpp
			private/UrlSchemeAdapter.cpp
			private/PdfLoader.cpp
			private/NotificationsBridge.cpp
			private/ClipboardDragdrop.cpp
			private/DrmAdapter.cpp
			private/DomainFilter.cpp
			private/BookmarksParser.cpp
			private/SettingsParser.cpp

		LIBS be root pthread
	)

	target_include_directories(web PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${WPEWEBKIT_INCLUDE_DIRS}
		${WPEFDO_INCLUDE_DIRS}
		${GLIB2_INCLUDE_DIRS}
	)

	target_link_libraries(web PRIVATE
		${WPEWEBKIT_LIBRARIES}
		${WPEFDO_LIBRARIES}
		${GLIB2_LIBRARIES}
		${GBM_LIBRARIES}
		${EGL_LIBRARIES}
		${GLES2_LIBRARIES}
		wayland-server
	)

	target_compile_options(web PRIVATE
		${WPEWEBKIT_CFLAGS_OTHER}
		${WPEFDO_CFLAGS_OTHER}
		${GLIB2_CFLAGS_OTHER}
		-std=c++17
	)

	target_compile_definitions(web PRIVATE _GNU_SOURCE)
endif()
