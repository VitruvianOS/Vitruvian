macro( DeclareDependency name )
	set( _OPTIONS_ARGS )
	set( _ONE_VALUE_ARGS )
	set( _MULTI_VALUE_ARGS LIBRARIES INCLUDES PACKAGES RUNTIMES )

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
	set (DEP_LIST "${DEP_LIST} ${_DECLAREDEPENDENCY_PACKAGES}")
	set (RUN_LIST ${RUN_LIST} ${_DECLAREDEPENDENCY_RUNTIMES})

endmacro()

set(BUILD_ICU ON)
set(ICU_BUILD_VERSION 57.2)
set(HEADERS_PATH_BASE "/usr/include")

add_subdirectory(external/icu-cmake)

DeclareDependency(
	GNUC
	RUNTIMES	"libc6 (>= 2.28)"
)

DeclareDependency(
	GNUCPP
	RUNTIMES	"libstdc++6 (>= 5.2)"
)

DeclareDependency(
	FLEX
	RUNTIMES	"libfl2 (>= 2.5.33)"
)

DeclareDependency(
	EXR
	LIBRARIES	"IlmImf;Half"
	PACKAGES	"libopenexr-dev"
	RUNTIMES	"libopenexr23 (>= 2.2.1)"
	INCLUDES	"${HEADERS_PATH_BASE}/OpenEXR/"
)

DeclareDependency(
	FREETYPE
	LIBRARIES	"freetype"
	PACKAGES	"libfreetype6-dev"
	RUNTIMES	"libfreetype6 (>= 2.2.1)"
)

DeclareDependency(
	GIF
	LIBRARIES	"gif"
	PACKAGES	"libgif-dev"
	RUNTIMES	"libgif7 (>= 5.1.4)"
)

DeclareDependency(
	ICNS
	LIBRARIES	"icns"
	PACKAGES	"libicns-dev"
	RUNTIMES	"libicns1 (>= 0.8.1)"
)

DeclareDependency(
	INPUT
	LIBRARIES	"input"
	PACKAGES	"libinput-dev"
	RUNTIMES	"libinput10 (>= 0.15.0)"
)

DeclareDependency(
	NOTO
	RUNTIMES	"fonts-noto-core;fonts-noto-extra;fonts-noto-mono"
)

DeclareDependency(
	JPEG
	LIBRARIES	"jpeg"
	PACKAGES	"libjpeg-dev"
	RUNTIMES	"libjpeg62-turbo (>= 1.5.2)"
)

DeclareDependency(
	PNG
	LIBRARIES	"png"
	PACKAGES	"libpng-dev"
	RUNTIMES	"libpng16-16 (>= 1.6.36)"
	INCLUDES	"${HEADERS_PATH_BASE}/libpng/"
)

DeclareDependency(
	TIFF
	LIBRARIES	"tiff"
	PACKAGES	"libtiff-dev"
	RUNTIMES	"libtiff5 (>= 4.1.0)"
)

DeclareDependency(
	TINFO
	RUNTIMES	"libtinfo6 (>= 6)"
)

DeclareDependency(
	UDEV
	LIBRARIES	"udev"
	PACKAGES	"libudev-dev"
	RUNTIMES	"libudev1 (>= 183)"
)

DeclareDependency(
	WEBP
	LIBRARIES	"webp"
	PACKAGES	"libwebp-dev"
	RUNTIMES	"libwebp6 (>= 0.6.1)"
)

DeclareDependency(
	ZLIB
	LIBRARIES  "z"
	PACKAGES  "zlib1g-dev"
	RUNTIMES  "zlib1g (>= 1:1.1.4)"
)

file(WRITE build/build_deps.txt "${DEP_LIST}")
