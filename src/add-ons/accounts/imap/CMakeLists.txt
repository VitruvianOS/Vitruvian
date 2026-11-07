AddOn(ImapProvider
	SHARED

	SOURCES
	ImapProvider.cpp

	LIBS be root
)

set_target_properties(ImapProvider PROPERTIES
	PREFIX ""
	OUTPUT_NAME "imap"
	SUFFIX ".so"
)

install(TARGETS ImapProvider DESTINATION /system/add-ons/accounts/)
