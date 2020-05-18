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
	shape/PathContainer.cpp
	shape/ShapeContainer.cpp
	shape/Shape.cpp
	shape/VectorPath.cpp
	style/GradientTransformable.cpp
	style/StyleContainer.cpp
	style/Style.cpp
	transformable/Transformable.cpp 

	transformer/AffineTransformer.cpp
	transformer/ContourTransformer.cpp
	transformer/PathSource.cpp
	transformer/PerspectiveTransformer.cpp
	transformer/StrokeTransformer.cpp
	transformer/Transformer.cpp
	transformer/TransformerFactory.cpp

	LIBS
	agg

	INCLUDES
	"shape/"
	"flat_icon/"
	"message/"
	"style/"
	"transformable/"
	"transformer/"
)

UsePrivateHeaders( icon shared )
