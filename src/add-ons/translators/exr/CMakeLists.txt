
Application(
	EXRTranslator

	SOURCES
	ConfigView.cpp
	EXRGamma.cpp
	EXRTranslator.cpp
	IStreamWrapper.cpp
	main.cpp

	LIBS be translation translatorutils

	INCLUDES
	"../shared"
	include_directories("/usr/include/OpenEXR/")
)
