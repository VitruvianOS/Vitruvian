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
