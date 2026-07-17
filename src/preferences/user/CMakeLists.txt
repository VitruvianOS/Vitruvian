Application(User
	SOURCES
	User.cpp
	UserWindow.cpp
	PasswordDialog.cpp
	LIBS shared localestub pam
	RDEF User.rdef
)
UsePrivateHeaders(User app interface shared support)
