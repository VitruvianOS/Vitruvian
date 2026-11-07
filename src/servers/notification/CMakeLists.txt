Server(
	notification_server
	SOURCES
	AppGroupView.cpp
	NotificationServer.cpp
	NotificationView.cpp
	NotificationWindow.cpp

	INCLUDES 
	LIBS translation notification localestub
	RDEF notification_server.rdef
)

UsePrivateHeaders(notification_server app notification)

AddOn(
	notification
	STATIC

	SOURCES
	AppUsage.cpp
	NotificationReceived.cpp
	Notifications.cpp
)

UsePrivateHeaders(notification notification)
DoCatalogs("x-vnd.vos-notification_server" servers/notification)

Server(
	notifications_bridge
	SOURCES
	NotificationsBridge.cpp

	INCLUDES
	LIBS translation
	RDEF notifications_bridge.rdef
)

find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
	pkg_check_modules(GIO REQUIRED gio-2.0)
	target_include_directories(notifications_bridge PRIVATE ${GIO_INCLUDE_DIRS})
	target_link_libraries(notifications_bridge PRIVATE ${GIO_LIBRARIES})
	target_compile_options(notifications_bridge PRIVATE ${GIO_CFLAGS_OTHER})
endif()
