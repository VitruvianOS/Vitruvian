AddOn(
	icon
	STATIC

	SOURCES
	Icon.cpp
	IconRenderer.cpp
	IconUtils.cpp
	flat_icon/FlatIconFormat.cpp
	flat_icon/FlatIconImporter.cpp
	flat_icon/LittleEndianBuffer.cpp
	flat_icon/PathCommandQueue.cpp

	message/Defines.cpp
	message/MessageImporter.cpp
	shape/PathSourceShape.cpp
	shape/ReferenceImage.cpp
	shape/Shape.cpp
	shape/VectorPath.cpp
	style/GradientTransformable.cpp
	style/Style.cpp
	transformable/Transformable.cpp 

	transformer/AffineTransformer.cpp
	transformer/CompoundStyleTransformer.cpp
	transformer/ContourTransformer.cpp
	transformer/PathSource.cpp
	transformer/PerspectiveTransformer.cpp
	transformer/StrokeTransformer.cpp
	transformer/StyleTransformer.cpp
	transformer/TransformerFactory.cpp

	LIBS
	agg

	INCLUDES
	"shape/"
	"flat_icon/"
	"generic"
	"message/"
	"style/"
	"transformable/"
	"transformer/"
)

UsePrivateHeaders( icon shared )
