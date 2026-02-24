Application(
	AboutSystem

	SOURCES
	AboutSystem.cpp
	HyperTextActions.cpp
	HyperTextView.cpp
	Utilities.cpp
	LIBS be shared translation translatorutils textencoding localestub
	RDEF AboutSystem.rdef
)

UsePrivateHeaders(AboutSystem interface app shared libroot)
