RunnableAddOn(LocalSend
	SOURCES
		LocalSendApp.cpp
		LocalSendWindow.cpp
		LocalSendManager.cpp
		LocalSendSettings.cpp
		DeviceListItem.cpp
		ReceivePromptWindow.cpp
		SettingsWindow.cpp
		BeHttpTransport.cpp
		protocol/LsJson.cpp
		protocol/LsSha256.cpp
		protocol/LsDevice.cpp
		protocol/LsFingerprint.cpp
		protocol/LsDiscovery.cpp
		protocol/LsHttpServer.cpp
		protocol/LsHttpTransport.cpp
		protocol/LsSenderSession.cpp
		protocol/LsReceiverSession.cpp
	LIBS be tracker
	RDEF LocalSend.rdef
)

target_include_directories(LocalSend PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/protocol
	${CMAKE_SOURCE_DIR}/headers/private/netservices
)

UsePrivateHeaders(LocalSend net)
