
TranslatorAddOn(
	JPEGTranslator SHARED

	SOURCES
	be_jdatadst.cpp
	be_jdatasrc.cpp
	be_jerror.cpp
	exif_parser.cpp
	JPEGTranslator.cpp

	LIBS be translation translatorutils localestub ${JPEG_LIBRARIES}

	INCLUDES
	"../shared"
	"../raw"

	RDEF JPEGTranslator.rdef
)
DoCatalogs("x-vnd.Haiku-JPEGTranslator" add-ons/translators/jpeg)
