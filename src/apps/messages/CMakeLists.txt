Application(Messages
	SOURCES
	Messages.cpp
	MessageStore.cpp
	MessagesWindow.cpp
	LIBS be localestub
	RDEF Messages.rdef
)

target_include_directories(Messages PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
