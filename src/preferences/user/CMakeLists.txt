Application(User
	SOURCES
	User.cpp
	UserWindow.cpp
	UserPictureView.cpp
	PasswordDialog.cpp
	${CMAKE_SOURCE_DIR}/src/preferences/common/AccountUtil.cpp
	LIBS shared localestub pam translation tracker
	INCLUDES ${CMAKE_SOURCE_DIR}/src/preferences/common
	RDEF User.rdef
)
UsePrivateHeaders(User app interface shared support)
