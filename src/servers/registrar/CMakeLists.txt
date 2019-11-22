find_package(Freetype REQUIRED)
include_directories(${FREETYPE_INCLUDE_DIRS})

add_executable(registrar)

target_include_directories(registrar PUBLIC
	"./"
	"mime"
)

#TODO: how to specify a full path?
#target_include_directories(app_server PRIVATE
#	"../../../headers/private/interface"
#)

target_sources(registrar PRIVATE
	AppInfoList.cpp
	AppInfoListMessagingTargetSet.cpp
	AuthenticationManager.cpp
	Clipboard.cpp
	ClipboardHandler.cpp
	Event.cpp
	EventMaskWatcher.cpp
	EventQueue.cpp
	MessageDeliverer.cpp
	MessageEvent.cpp
	MessageHandler.cpp
	MessageRunnerManager.cpp
	MessagingService.cpp
	MIMEManager.cpp
	#PackageWatchingManager.cpp
	PriorityMessageQueue.cpp
	RecentApps.cpp
	RecentEntries.cpp
	Registrar.cpp
	RosterAppInfo.cpp
	RosterSettingsCharStream.cpp
	ShutdownProcess.cpp
	TRoster.cpp
	Watcher.cpp
	WatchingService.cpp

	# mime
	mime/CreateAppMetaMimeThread.cpp
	mime/MimeUpdateThread.cpp
	mime/RegistrarThread.cpp
	mime/RegistrarThreadManager.cpp
	mime/UpdateMimeInfoThread.cpp
)

target_link_libraries(registrar PRIVATE be root)
