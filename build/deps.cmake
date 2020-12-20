macro( DeclareDependency name )
	set( _OPTIONS_ARGS )
	set( _ONE_VALUE_ARGS )
	set( _MULTI_VALUE_ARGS LIBRARIES INCLUDES PACKAGES )

	cmake_parse_arguments(
		_DECLAREDEPENDENCY
		"${_OPTIONS_ARGS}"
		"${_ONE_VALUE_ARGS}"
		"${_MULTI_VALUE_ARGS}"
		${ARGN}
	)

	set ("${name}_LIBRARIES" "${_DECLAREDEPENDENCY_LIBRARIES}")
	set ("${name}_INCLUDES" "${_DECLAREDEPENDENCY_INCLUDES}")

	list (APPEND PACKAGES_LIST "${_DECLAREDEPENDENCY_PACKAGES}")
endmacro()

set(BUILD_ICU ON)
set(ICU_BUILD_VERSION 57.2)
set(HEADERS_PATH_BASE "/usr/include")

set(EXR_LIBRARIES "IlmImf;Half")
set(EXR_INCLUDES "${HEADERS_PATH_BASE}/OpenEXR/")

set(GIF_LIBRARIES "gif")

set(ICNS_LIBRARIES "icns")

set(INPUT_LIBRARIES "input")

set(JPEG_LIBRARIES "jpeg")

set(TIFF_LIBRARIES "tiff")

set(UDEV_LIBRARIES "udev")

set(WEBP_LIBRARIES "webp")


add_subdirectory(external/icu-cmake)

find_package(ZLIB REQUIRED)


DeclareDependency(
	FREETYPE
	LIBRARIES	"freetype"
	PACKAGES	"libfreetype-dev"
)

DeclareDependency(
	PNG
	LIBRARIES	"png"
	PACKAGES	"libpng-dev"
	INCLUDES	"${HEADERS_PATH_BASE}/libpng/"
)

#message(STATUS "Dependencies: ${PACKAGES_LIST}")
