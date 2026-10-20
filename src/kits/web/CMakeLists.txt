# Web kit — WPE WebKit2 BWebView wrapper for Vitruvian
#
# Two backends paint into app_server:
#  - WPEPlatform (WPE >= 2.54, the testing lane): our own WPEDisplay /
#    WPEView implementation; no wpebackend-fdo, no libwayland-server.
#  - FDO (older WPE, the trixie lane): the wpebackend-fdo exportable
#    with its in-process SHM path, kept so the lane still builds.
# When no WPE packages are found a stub library is built so the netplus
# app still links.

find_package(PkgConfig)

set(WEB_SOURCES
	UrlNormalize.cpp
	WebBookmarks.cpp
	WebExtensions.cpp
	WebSettings.cpp
	WebView.cpp
)

# Pure key=value / bookmark parsers: no WebKit dependency, compiled
# always so the public settings and bookmarks facades work even when
# WPE is not linked.
set(WEB_CORE_PRIVATE_SOURCES
	private/BookmarksParser.cpp
	private/SettingsParser.cpp
)

set(WEB_PRIVATE_SOURCES "")

if(PKG_CONFIG_FOUND)
	# Prefixes must not collide with the WPE_WEBKIT DeclareDependency in
	# build/deps.cmake, which clobbers same-named variables at include time.
	pkg_check_modules(VOS_WEBKIT2 wpe-webkit-2.0 QUIET)
	# Newer WPE splits wpe-platform out (2.54 on, the testing lane).
	pkg_check_modules(VOS_WPE_PLATFORM wpe-platform-2.0 QUIET)
	pkg_check_modules(VOS_LIBWPE wpe-1.0 QUIET)
	pkg_check_modules(VOS_WPE_FDO wpebackend-fdo-1.0 QUIET)
	pkg_check_modules(VOS_GLIB glib-2.0 QUIET)

	# WEB_BACKEND picks the lane: auto prefers WPEPlatform when the
	# headers are installed, then FDO; "platform" or "fdo" force one.
	set(WEB_BACKEND "auto" CACHE STRING
		"Web kit backend: auto, platform (WPEPlatform) or fdo")

	set(VOS_WEB_BACKEND "")
	if(VOS_WEBKIT2_FOUND AND VOS_LIBWPE_FOUND)
		if(NOT WEB_BACKEND STREQUAL "fdo" AND VOS_WPE_PLATFORM_FOUND)
			set(VOS_WEB_BACKEND "platform")
		elseif(NOT WEB_BACKEND STREQUAL "platform" AND VOS_WPE_FDO_FOUND)
			set(VOS_WEB_BACKEND "fdo")
		endif()
	endif()

	if(VOS_WEB_BACKEND)
		set(WEB_PRIVATE_SOURCES
			private/ClipboardDragdrop.cpp
			private/CompositorAdapter.cpp
			private/ContentFilterStore.cpp
			private/DomainFilter.cpp
			private/DownloadHandler.cpp
			private/DownloadSanitizer.cpp
			private/EventAdapter.cpp
			private/FocusAndCursor.cpp
			private/FrameNotifier.cpp
			private/KeyTables.cpp
			private/Logging.cpp
			private/NotificationsBridge.cpp
			private/ProcessManager.cpp
			private/SessionAdapter.cpp
			private/UrlSchemeAdapter.cpp
			private/WebkitBridge.cpp
		)
		if(VOS_WEB_BACKEND STREQUAL "platform")
			list(APPEND WEB_PRIVATE_SOURCES private/vos_platform.cpp)
			message(STATUS "web kit: backend WPEPlatform (native)")
		else()
			message(STATUS "web kit: backend wpebackend-fdo (legacy)")
			pkg_check_modules(VOS_WAYLAND wayland-server QUIET)
		endif()
	else()
		message(STATUS "web kit: WPE packages not found, building stubs")
	endif()
endif()

add_library(web SHARED ${WEB_SOURCES} ${WEB_CORE_PRIVATE_SOURCES}
	${WEB_PRIVATE_SOURCES})

target_include_directories(web PUBLIC
	${PROJECT_SOURCE_DIR}/headers
	${PROJECT_SOURCE_DIR}/headers/os
)

target_include_directories(web PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/private
)

if(WEB_PRIVATE_SOURCES)
	target_include_directories(web PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/private
		${PROJECT_SOURCE_DIR}/headers/private/netservices
		${VOS_WEBKIT2_INCLUDE_DIRS}
		${VOS_WPE_PLATFORM_INCLUDE_DIRS}
		${VOS_LIBWPE_INCLUDE_DIRS}
		${VOS_GLIB_INCLUDE_DIRS}
	)
	if(VOS_WEB_BACKEND STREQUAL "fdo")
		target_include_directories(web PRIVATE
			${VOS_WPE_FDO_INCLUDE_DIRS}
			${VOS_WAYLAND_INCLUDE_DIRS}
		)
	endif()
	target_compile_definitions(web PRIVATE HAS_WPE=1)
	if(VOS_WEB_BACKEND STREQUAL "platform")
		target_compile_definitions(web PRIVATE HAS_WPE_PLATFORM=1)
	endif()
	target_compile_options(web PRIVATE
		${VOS_WEBKIT2_CFLAGS_OTHER}
		${VOS_WPE_PLATFORM_CFLAGS_OTHER}
		${VOS_LIBWPE_CFLAGS_OTHER}
		${VOS_GLIB_CFLAGS_OTHER}
	)
	if(VOS_WEB_BACKEND STREQUAL "fdo")
		target_compile_options(web PRIVATE
			${VOS_WPE_FDO_CFLAGS_OTHER}
			${VOS_WAYLAND_CFLAGS_OTHER}
		)
	endif()
	# BFilePanel lives in the tracker kit, like in the legacy API.
	target_link_libraries(web PRIVATE
		be root pthread tracker
		${VOS_WEBKIT2_LIBRARIES}
		${VOS_LIBWPE_LIBRARIES}
		${VOS_GLIB_LIBRARIES}
	)
	if(VOS_WEB_BACKEND STREQUAL "fdo")
		target_link_libraries(web PRIVATE
			${VOS_WPE_FDO_LIBRARIES}
			${VOS_WAYLAND_LIBRARIES}
		)
	endif()
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
