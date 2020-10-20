
Application(
	EXRTranslator

	SOURCES
	ConfigView.cpp
	EXRGamma.cpp
	EXRTranslator.cpp
	IStreamWrapper.cpp
	main.cpp

	LIBS be translation translatorutils IlmImf Half

	INCLUDES
	"../shared"
	"/usr/include/OpenEXR/"
)
