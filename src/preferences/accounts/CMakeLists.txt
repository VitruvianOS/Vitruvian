Application(Accounts
	SOURCES
	Accounts.cpp
	AccountsWindow.cpp
	MailAccountWindow.cpp
	LIBS mailaccounts localestub
	RDEF Accounts.rdef
)
target_include_directories(Accounts PRIVATE
	${PROJECT_SOURCE_DIR}/headers/private/preferences
)
