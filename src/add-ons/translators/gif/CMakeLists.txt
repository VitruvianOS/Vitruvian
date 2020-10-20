
Application(
	GIFTranslator

	SOURCES
	GIFLoad.cpp	
	GIFSave.cpp
	GIFTranslator.cpp
	GIFView.cpp
	LoadPalette.cpp
	SavePalette.cpp
	SFHash.cpp

	LIBS be translation translatorutils gif

	INCLUDES
	"../shared"
	"/usr/include/libgif"
)
