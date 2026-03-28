Application(
	AboutSystem

	SOURCES
	AboutSystem.cpp
	HyperTextActions.cpp
	HyperTextView.cpp
	Utilities.cpp
	LIBS shared translation translatorutils textencoding localestub
	RDEF AboutSystem.rdef
)

UsePrivateHeaders(AboutSystem interface app shared libroot)

target_compile_definitions(AboutSystem PRIVATE VOS_VERSION="${PROJECT_VERSION}")
