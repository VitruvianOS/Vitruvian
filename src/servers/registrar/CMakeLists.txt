include_directories(${FREETYPE_INCLUDES})

Server(
	registrar

	SOURCES
	AppInfoList.cpp
	AppInfoListMessagingTargetSet.cpp
	#AuthenticationManager.cpp
	Clipboard.cpp
	ClipboardHandler.cpp
	Event.cpp
	EventMaskWatcher.cpp
	EventQueue.cpp
	MessageDeliverer.cpp
	MessageEvent.cpp
	MessageHandler.cpp
	MessageRunnerManager.cpp
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

	INCLUDES
	"mime"
	LIBS localestub
	RDEF registrar.rdef
)

UsePrivateHeaders(registrar app kernel libroot system tracker)
