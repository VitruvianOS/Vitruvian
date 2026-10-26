AddOn(
	plaintext
	SHARED

	SOURCES
	Catalog.cpp

	LIBS be root
)

set_target_properties(plaintext PROPERTIES PREFIX "" SUFFIX "")

UsePrivateHeaders(plaintext locale shared storage support app interface)
