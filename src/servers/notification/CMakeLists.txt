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
