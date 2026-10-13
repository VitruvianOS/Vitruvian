# Web kit — WPE WebKit2 BWebView wrapper for Vitruvian
#
# When WPE packages are available the compositor/event/bridge layer
# from private/ is compiled in and BWebView delegates to it.
# Otherwise a stub library is built so the netplus app still links.

find_package(PkgConfig)

set(WEB_SOURCES
	UrlNormalize.cpp
	WebView.cpp
)

set(WEB_PRIVATE_SOURCES "")

if(PKG_CONFIG_FOUND)
	# wpebackend-fdo provides the web-process renderer backend until the
	# in-tree wpe-backend-vos implements the WPE loader interface.
	# Prefixes must not collide with the WPE_WEBKIT DeclareDependency in
	# build/deps.cmake, which clobbers same-named variables at include time.
	pkg_check_modules(VOS_WEBKIT2 wpe-webkit-2.0 QUIET)
	pkg_check_modules(VOS_LIBWPE wpe-1.0 QUIET)
	pkg_check_modules(VOS_WPE_FDO wpebackend-fdo-1.0 QUIET)
	pkg_check_modules(VOS_GLIB glib-2.0 QUIET)
	pkg_check_modules(VOS_WAYLAND wayland-server QUIET)

	if(VOS_WEBKIT2_FOUND AND VOS_LIBWPE_FOUND AND VOS_WPE_FDO_FOUND)
		set(WEB_PRIVATE_SOURCES
			private/BookmarksParser.cpp
			private/ClipboardDragdrop.cpp
			private/CompositorAdapter.cpp
			private/DomainFilter.cpp
			private/DownloadHandler.cpp
			private/DownloadSanitizer.cpp
			private/EventAdapter.cpp
			private/FocusAndCursor.cpp
			private/FrameNotifier.cpp
			private/KeyTables.cpp
			private/Logging.cpp
			private/NotificationsBridge.cpp
			private/PdfLoader.cpp
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
		${VOS_WEBKIT2_INCLUDE_DIRS}
		${VOS_LIBWPE_INCLUDE_DIRS}
		${VOS_WPE_FDO_INCLUDE_DIRS}
		${VOS_GLIB_INCLUDE_DIRS}
		${VOS_WAYLAND_INCLUDE_DIRS}
	)
	target_compile_definitions(web PRIVATE HAS_WPE=1)
	target_compile_options(web PRIVATE
		${VOS_WEBKIT2_CFLAGS_OTHER}
		${VOS_LIBWPE_CFLAGS_OTHER}
		${VOS_WPE_FDO_CFLAGS_OTHER}
		${VOS_GLIB_CFLAGS_OTHER}
		${VOS_WAYLAND_CFLAGS_OTHER}
	)
	# BFilePanel lives in the tracker kit, like in the legacy API.
	target_link_libraries(web PRIVATE
		be root pthread tracker
		${VOS_WEBKIT2_LIBRARIES}
		${VOS_LIBWPE_LIBRARIES}
		${VOS_WPE_FDO_LIBRARIES}
		${VOS_GLIB_LIBRARIES}
		${VOS_WAYLAND_LIBRARIES}
	)
else()
	target_link_libraries(web PRIVATE be root pthread)
endif()

set_target_properties(web PROPERTIES
	COMPILE_FLAGS "-include LinuxBuildCompatibility.h"
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED ON
	VERSION 1.0.0
	SOVERSION 1
)

install(TARGETS web DESTINATION /system/lib/)
