Application(
	SGITranslator

	SOURCES
	SGIImage.cpp
	SGIMain.cpp
	SGITranslator.cpp
	SGIView.cpp

	LIBS be translation translatorutils localestub

	INCLUDES
	"../shared"

	RDEF SGITranslator.rdef
)
