Application(Administrator
	SOURCES
	Administrator.cpp
	AdministratorWindow.cpp
	${CMAKE_SOURCE_DIR}/src/preferences/common/AccountUtil.cpp
	LIBS shared localestub
	INCLUDES ${CMAKE_SOURCE_DIR}/src/preferences/common
	RDEF Administrator.rdef
)
UsePrivateHeaders(Administrator app interface shared support)
