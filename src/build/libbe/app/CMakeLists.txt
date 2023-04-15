
set (APP_SOURCES ${KITS_SOURCES}/app/)

target_sources(be_build PRIVATE
 	Application.cpp
 	AppMisc.cpp
	Looper.cpp
	Message.cpp
	${APP_SOURCES}MessageAdapter.cpp
 	Messenger.cpp
 	${APP_SOURCES}MessageUtils.cpp
 	${APP_SOURCES}TypeConstants.cpp
)
