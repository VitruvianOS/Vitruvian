Application(Users
	SOURCES
	Users.cpp
	UsersWindow.cpp
	UserPictureView.cpp
	PasswordDialog.cpp
	${CMAKE_SOURCE_DIR}/src/preferences/common/AccountUtil.cpp
	LIBS shared localestub pam translation tracker
	INCLUDES ${CMAKE_SOURCE_DIR}/src/preferences/common
	RDEF Users.rdef
)
UsePrivateHeaders(Users app interface shared support)
DoCatalogs("x-vnd.vos-Users" preferences/users)
