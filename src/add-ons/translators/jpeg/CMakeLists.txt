
Application(
	JPEGTranslator

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
)
