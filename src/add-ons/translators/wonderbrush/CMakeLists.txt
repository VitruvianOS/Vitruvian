Application(
	WonderbrushTranslator

	SOURCES
	support/bitmap_compression.cpp
	support/blending.cpp
	support/lab_convert.cpp
	Canvas.cpp
	Layer.cpp
	WonderBrushImage.cpp
	WonderBrushMain.cpp
	WonderBrushTranslator.cpp
	WonderBrushView.cpp

	LIBS be translation translatorutils localestub ${ZLIB_LIBRARIES}

	INCLUDES
	"../shared"
	"support"
)

