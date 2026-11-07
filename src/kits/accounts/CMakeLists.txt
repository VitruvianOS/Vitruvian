target_sources(be PRIVATE
	libaccounts/Account.cpp
	libaccounts/AccountProvider.cpp
	libaccounts/AccountRequest.cpp
	libaccounts/AccountRoster.cpp
	libaccounts/AccountsDefs.cpp
	libaccounts/SipAccounts.cpp
)

# The SIP provider talks to the telephony kit's SIP backend.
target_include_directories(be PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/libaccounts
	${CMAKE_SOURCE_DIR}/src/kits/telephony/libtelephony
	${CMAKE_SOURCE_DIR}/headers/os/account
)

# Mail provider catalog and autoconfig, shared by WebMail and the Accounts
# preflet. Kept out of libbe so it does not pull in curl.
AddOn(
	mailaccounts
	STATIC

	SOURCES
	libaccounts/MailAutoconfig.cpp
	libaccounts/MailProviders.cpp
	LIBS be curl resolv
)
target_include_directories(mailaccounts PUBLIC
	${CMAKE_SOURCE_DIR}/headers/private/accounts
)
