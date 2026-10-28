Application(Phone
	SOURCES
	Phone.cpp
	CallLog.cpp
	CallAudioRouter.cpp
	SipAudioRouter.cpp
	DialerWindow.cpp
	InCallWindow.cpp
	IncomingCallWindow.cpp
	LIBS be localestub media2
	RDEF Phone.rdef
)

target_include_directories(Phone PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
UsePrivateHeaders(Phone interface)
