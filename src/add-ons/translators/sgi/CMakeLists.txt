
TranslatorAddOn(
	SGITranslator SHARED

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
DoCatalogs("x-vnd.vos-SGITranslator" add-ons/translators/sgi)
