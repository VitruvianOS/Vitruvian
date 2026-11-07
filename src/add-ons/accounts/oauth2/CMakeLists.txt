find_package(OpenSSL REQUIRED)

AddOn(Oauth2Provider
	SHARED

	SOURCES
	HttpsClient.cpp
	Oauth2Flow.cpp
	Oauth2Provider.cpp

	LIBS be root OpenSSL::SSL OpenSSL::Crypto
)

set_target_properties(Oauth2Provider PROPERTIES
	PREFIX ""
	OUTPUT_NAME "oauth2"
	SUFFIX ".so"
)

install(TARGETS Oauth2Provider DESTINATION /system/add-ons/accounts/)
