Application(
	WebMail

	SOURCES
	main.cpp
	WebMailApp.cpp
	MainWindow.cpp
	EmptyStateView.cpp
	MailView.cpp
	SidebarView.cpp
	SettingsWindow.cpp
	Accounts.cpp
	UnreadParser.cpp
	OAuth2.cpp
	protocol/ImapClient.cpp
	protocol/Pop3Client.cpp
	protocol/SmtpClient.cpp
	protocol/MimeBuilder.cpp
	protocol/MailSession.cpp

	LIBS mailaccounts web tracker etpan curl gnutls resolv localestub

	RDEF WebMail.rdef
)

target_include_directories(WebMail PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${PROJECT_SOURCE_DIR}/headers
	${PROJECT_SOURCE_DIR}/headers/os
	${PROJECT_SOURCE_DIR}/headers/os/account
	${PROJECT_SOURCE_DIR}/headers/private/preferences
)

find_package(PkgConfig)
pkg_check_modules(WEBMAIL_GLIB glib-2.0)
pkg_check_modules(WEBMAIL_WEBKIT wpe-webkit-2.0)
target_include_directories(WebMail PRIVATE
	${WEBMAIL_GLIB_INCLUDE_DIRS}
	${WEBMAIL_WEBKIT_INCLUDE_DIRS}
)
target_compile_options(WebMail PRIVATE
	${WEBMAIL_GLIB_CFLAGS_OTHER}
	${WEBMAIL_WEBKIT_CFLAGS_OTHER}
)

# std::filesystem for the profile and ui paths.
set_target_properties(WebMail PROPERTIES
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED ON
)

# The bundled native UI, served over vos-mail:. Install under /system/data
# rather than /system/apps/WebMail: the Deskbar Applications symlink points
# at /system/apps/WebMail and a directory there shows up as a stray entry.
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ui
	DESTINATION /system/data/WebMail
)
