
set (INTERFACE_SOURCES ${KITS_SOURCES}/interface/)

target_sources(be_build PRIVATE
	Bitmap.cpp
	${INTERFACE_SOURCES}/Gradient.cpp
	${INTERFACE_SOURCES}/GradientLinear.cpp
	${INTERFACE_SOURCES}/GradientRadial.cpp
	${INTERFACE_SOURCES}/GradientRadialFocus.cpp
	${INTERFACE_SOURCES}/GradientDiamond.cpp
	${INTERFACE_SOURCES}/GradientConic.cpp
 	GraphicsDefs.cpp
 	${INTERFACE_SOURCES}/Point.cpp
 	${INTERFACE_SOURCES}/Rect.cpp
 	${INTERFACE_SOURCES}/Region.cpp
 	${INTERFACE_SOURCES}/RegionSupport.cpp
 	SystemPalette.cpp
)

